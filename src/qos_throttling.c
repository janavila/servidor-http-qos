/**
 * qos_throttling.c - Envio de arquivos com controle de taxa (MVP2, Membro 1)
 *
 * Algoritmo (pacing por prazo absoluto):
 *
 *   deadline = agora
 *   para cada bloco de n bytes:
 *       send(bloco)
 *       deadline += n / taxa            <- instante em que o PRÓXIMO bloco pode sair
 *       dorme até `deadline`            <- clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)
 *
 * Por que prazo absoluto e não "dormir n/taxa a cada bloco": o tempo gasto em
 * fread()/send() e a imprecisão do sono se acumulariam e a taxa real ficaria
 * abaixo da pedida. Somando ao prazo, o atraso de um bloco é compensado no
 * seguinte e a média converge para a taxa configurada. O sono absoluto também
 * evita recalcular o tempo restante depois de uma interrupção por sinal.
 *
 * O primeiro bloco sai sem espera (rajada de no máximo um bloco). Se o envio
 * ficar mais de QOS_MAX_LAG_NS atrasado (cliente lento, send() bloqueado), o
 * prazo é reiniciado para não gerar uma rajada longa depois.
 */
#define _POSIX_C_SOURCE 200809L /* clock_nanosleep, strcasecmp (com -std=c11) */

#include "qos_throttling.h"

#include "qos_config.h"

#include <errno.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>

#define NS_PER_SEC 1000000000LL
#define QOS_FAST_CHUNK 16384u /* bloco para arquivos isentos (HTML) */
#define QOS_MAX_CHUNK 8192u   /* bloco máximo com limitação */
#define QOS_MIN_CHUNK 256u    /* bloco mínimo (taxas muito baixas) */
#define QOS_MAX_LAG_NS NS_PER_SEC

/* ----------------------------------------------------------------- tempo */

static int mono_now_ns(int64_t *out)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return -1;
    *out = (int64_t)ts.tv_sec * NS_PER_SEC + ts.tv_nsec;
    return 0;
}

/* Dorme até o instante absoluto `deadline_ns` (CLOCK_MONOTONIC). Prazo no
 * passado retorna imediatamente. Reinicia sozinho se interrompido por sinal. */
static int sleep_until_ns(int64_t deadline_ns)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(deadline_ns / NS_PER_SEC);
    ts.tv_nsec = (long)(deadline_ns % NS_PER_SEC);

    int rc;
    do {
        rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
    } while (rc == EINTR);

    if (rc != 0) { /* clock_nanosleep devolve o código de erro, não usa errno */
        errno = rc;
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ envio */

/* send() até enviar tudo. MSG_NOSIGNAL evita SIGPIPE se o cliente fechar. */
static int send_all_fd(int fd, const unsigned char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = send(fd, buf, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        buf += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

/* Bloco de ~100 ms de dados, entre MIN e MAX: taxas baixas ganham blocos
 * menores e a transmissão fica suave em vez de em "degraus" longos. */
static size_t pick_chunk(uint64_t bytes_per_sec)
{
    uint64_t c = bytes_per_sec / 10u;
    if (c < QOS_MIN_CHUNK)
        c = QOS_MIN_CHUNK;
    if (c > QOS_MAX_CHUNK)
        c = QOS_MAX_CHUNK;
    return (size_t)c;
}

/* ------------------------------------------------------------ isenção HTML */

int qos_mime_is_exempt(const char *mime_type)
{
    return mime_type != NULL && strncasecmp(mime_type, "text/html", 9) == 0;
}

int qos_path_is_html(const char *path)
{
    if (!path)
        return 0;
    const char *dot = strrchr(path, '.');
    if (!dot)
        return 0;
    return strcasecmp(dot, ".html") == 0 || strcasecmp(dot, ".htm") == 0;
}

/* ------------------------------------------------------------- API pública */

ssize_t qos_send_file_throttled_dyn(int socket_fd, FILE *file, size_t file_size,
                                    const char *mime_type, qos_rate_fn rate_fn, void *ctx)
{
    if (socket_fd < 0 || file == NULL) {
        errno = EINVAL;
        return -1;
    }

    const int exempt = qos_mime_is_exempt(mime_type);
    unsigned char buf[QOS_FAST_CHUNK];
    size_t sent_total = 0;
    int64_t deadline;

    if (mono_now_ns(&deadline) != 0)
        return -1;

    while (sent_total < file_size) {
        uint64_t bps = 0;
        size_t chunk = QOS_FAST_CHUNK;

        if (!exempt) {
            uint32_t kbps = rate_fn ? rate_fn(ctx) : 0;
            if (kbps == 0)
                kbps = 1; /* nunca zero: evitaria divisão por zero e travaria o envio */
            bps = qos_kbps_to_bytes_per_sec(kbps);
            chunk = pick_chunk(bps);
        }

        size_t want = file_size - sent_total;
        if (want > chunk)
            want = chunk;

        size_t n = fread(buf, 1, want, file);
        if (n == 0) { /* EOF antes de file_size ou erro de leitura */
            errno = EIO;
            return -1;
        }

        if (send_all_fd(socket_fd, buf, n) != 0)
            return -1;
        sent_total += n;

        if (exempt)
            continue;

        deadline += (int64_t)((uint64_t)n * NS_PER_SEC / bps);

        int64_t now;
        if (mono_now_ns(&now) != 0)
            return -1;
        if (now - deadline > QOS_MAX_LAG_NS)
            deadline = now; /* muito atrasado: não compensa com rajada longa */

        if (sent_total < file_size && sleep_until_ns(deadline) != 0)
            return -1;
    }

    return (ssize_t)sent_total;
}

static uint32_t fixed_rate_cb(void *ctx)
{
    return *(const uint32_t *)ctx;
}

ssize_t qos_send_file_throttled(int socket_fd, FILE *file, size_t file_size,
                                const char *mime_type, uint32_t rate_kbps_per_conn)
{
    return qos_send_file_throttled_dyn(socket_fd, file, file_size, mime_type,
                                       fixed_rate_cb, &rate_kbps_per_conn);
}
