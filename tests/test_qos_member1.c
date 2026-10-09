/**
 * test_qos_member1.c - Testes do módulo de QoS (Membro 1), sem precisar do servidor.
 *
 * Compilar e executar (a partir da raiz do projeto):
 *   gcc -Isrc -std=c11 -Wall -Wextra -Wpedantic -O2 -pthread \
 *       tests/test_qos_member1.c src/qos_config.c src/qos_throttling.c \
 *       -o tests/test_qos_member1 && ./tests/test_qos_member1
 *
 * Cobre:
 *   1. parsing do arquivo (comentários, linhas inválidas, IP repetido, arquivo ausente);
 *   2. taxa padrão de 1000 kbps (IP desconhecido, NULL, texto inválido);
 *   3. taxa efetiva = taxa_do_IP / conexões ativas;
 *   4. pacing: 200000 bytes a 800 kbps devem levar ~2 s; o mesmo conteúdo como
 *      text/html deve sair quase instantaneamente. A integridade dos bytes é
 *      conferida por soma de verificação no lado receptor.
 */
#define _POSIX_C_SOURCE 200809L

#include "qos_config.h"
#include "qos_throttling.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int g_fail = 0;

#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (cond) {                                                   \
            printf("  [ OK  ] %s\n", msg);                            \
        } else {                                                      \
            printf("  [FALHA] %s (linha %d)\n", msg, __LINE__);       \
            g_fail++;                                                 \
        }                                                             \
    } while (0)

/* ----------------------------------------------------------- configuração */

static void test_config(void)
{
    printf("\n== Configuracao e taxa padrao\n");

    char path[] = "/tmp/qos_test_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        perror("mkstemp");
        g_fail++;
        return;
    }
    FILE *f = fdopen(fd, "w");
    if (!f) {
        perror("fdopen");
        close(fd);
        unlink(path);
        g_fail++;
        return;
    }
    fputs("# comentario de linha inteira\n"
          "192.168.0.10 2000\n"
          "192.168.0.11 1500   # comentario no fim da linha\n"
          "\n"
          "192.168.0.12 1200\n"
          "10.0.0.1 abc\n"          /* taxa nao numerica */
          "999.1.1.1 100\n"         /* IP invalido */
          "10.0.0.2 0\n"            /* taxa zero */
          "10.0.0.3 500 sobrando\n" /* texto extra */
          "10.0.0.4 -5\n"           /* taxa negativa */
          "192.168.0.10 2500\n",    /* repetido: vale este */
          f);
    fclose(f);

    CHECK(qos_load_config(path) == 0, "qos_load_config retorna 0");
    CHECK(qos_config_count() == 3, "3 IPs validos carregados (invalidos ignorados)");
    CHECK(qos_get_ip_limit_kbps("192.168.0.11") == 1500, "192.168.0.11 -> 1500 kbps");
    CHECK(qos_get_ip_limit_kbps("192.168.0.12") == 1200, "192.168.0.12 -> 1200 kbps");
    CHECK(qos_get_ip_limit_kbps("192.168.0.10") == 2500, "IP repetido: ultima ocorrencia (2500)");

    CHECK(qos_get_ip_limit_kbps("10.9.9.9") == QOS_DEFAULT_RATE_KBPS, "IP desconhecido -> 1000 kbps");
    CHECK(qos_get_ip_limit_kbps(NULL) == QOS_DEFAULT_RATE_KBPS, "NULL -> 1000 kbps");
    CHECK(qos_get_ip_limit_kbps("nao-e-ip") == QOS_DEFAULT_RATE_KBPS, "texto invalido -> 1000 kbps");
    CHECK(qos_get_ip_limit_kbps("10.0.0.1") == QOS_DEFAULT_RATE_KBPS, "linha invalida nao entrou (abc)");
    CHECK(qos_get_ip_limit_kbps("10.0.0.2") == QOS_DEFAULT_RATE_KBPS, "linha invalida nao entrou (taxa 0)");
    CHECK(qos_get_ip_limit_kbps("10.0.0.3") == QOS_DEFAULT_RATE_KBPS, "linha invalida nao entrou (texto extra)");
    CHECK(qos_get_ip_limit_kbps("10.0.0.4") == QOS_DEFAULT_RATE_KBPS, "linha invalida nao entrou (negativa)");

    printf("\n== Taxa efetiva por conexao\n");
    /* IP com 1000 kbps (padrao): 125000 B/s */
    CHECK(qos_effective_bytes_per_sec("10.9.9.9", 1) == 125000, "1000 kbps, 1 conexao  -> 125000 B/s");
    CHECK(qos_effective_bytes_per_sec("10.9.9.9", 2) == 62500, "1000 kbps, 2 conexoes -> 62500 B/s");
    CHECK(qos_effective_bytes_per_sec("10.9.9.9", 4) == 31250, "1000 kbps, 4 conexoes -> 31250 B/s");
    CHECK(qos_effective_bytes_per_sec("10.9.9.9", 0) == 125000, "0 conexoes tratado como 1");
    CHECK(qos_effective_bytes_per_sec("192.168.0.10", 5) == 62500, "2500 kbps, 5 conexoes -> 62500 B/s");
    CHECK(qos_effective_rate_kbps("10.9.9.9", 3) == 333, "1000 kbps / 3 -> 333 kbps");
    CHECK(qos_effective_rate_kbps("10.9.9.9", 100000) == 1, "piso de 1 kbps");

    printf("\n== Falha de leitura preserva a configuracao anterior\n");
    CHECK(qos_load_config("/tmp/qos_arquivo_que_nao_existe.txt") == -1, "arquivo ausente retorna -1");
    CHECK(qos_config_count() == 3 && qos_get_ip_limit_kbps("192.168.0.11") == 1500,
          "tabela anterior continua valendo");

    unlink(path);
}

/* ---------------------------------------------------------------- pacing */

typedef struct {
    int fd;
    size_t received;
    unsigned long sum;
} Reader;

static void *reader_main(void *arg)
{
    Reader *r = arg;
    unsigned char buf[65536];
    ssize_t n;
    while ((n = recv(r->fd, buf, sizeof buf, 0)) > 0) {
        for (ssize_t i = 0; i < n; i++)
            r->sum += buf[i];
        r->received += (size_t)n;
    }
    return NULL;
}

static double elapsed_s(const struct timespec *a, const struct timespec *b)
{
    return (double)(b->tv_sec - a->tv_sec) + (double)(b->tv_nsec - a->tv_nsec) / 1e9;
}

/* Envia `size` bytes por um socketpair e mede o tempo. Retorna 0 se os bytes
 * chegaram integros. */
static int run_transfer(const char *mime, uint32_t kbps, size_t size, double *elapsed)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        perror("socketpair");
        return -1;
    }

    FILE *tmp = tmpfile();
    if (!tmp) {
        perror("tmpfile");
        close(sv[0]);
        close(sv[1]);
        return -1;
    }
    unsigned long expected_sum = 0;
    for (size_t i = 0; i < size; i++) {
        unsigned char b = (unsigned char)((i * 31u + 7u) & 0xFFu);
        expected_sum += b;
        fputc(b, tmp);
    }
    fflush(tmp);
    rewind(tmp);

    Reader rd = {sv[1], 0, 0};
    pthread_t th;
    if (pthread_create(&th, NULL, reader_main, &rd) != 0) {
        fclose(tmp);
        close(sv[0]);
        close(sv[1]);
        return -1;
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    ssize_t sent = qos_send_file_throttled(sv[0], tmp, size, mime, kbps);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    shutdown(sv[0], SHUT_WR);
    pthread_join(th, NULL);
    close(sv[0]);
    close(sv[1]);
    fclose(tmp);

    *elapsed = elapsed_s(&t0, &t1);
    return (sent == (ssize_t)size && rd.received == size && rd.sum == expected_sum) ? 0 : -1;
}

static void test_pacing(void)
{
    printf("\n== Pacing (200000 bytes a 800 kbps = 100000 B/s)\n");

    const size_t size = 200000;
    const uint32_t kbps = 800;
    const double bps = 100000.0;
    const double expected = (double)size / bps;      /* 2,0 s */
    const double chunk_t = 8192.0 / bps;             /* o 1o bloco sai sem espera */
    const double lo = expected - chunk_t - 0.05;
    const double hi = expected * 1.10 + 0.10;
    double t = 0;

    int rc = run_transfer("image/jpeg", kbps, size, &t);
    printf("  .jpg: %.3f s (esperado ~%.2f s, aceito entre %.2f e %.2f)\n", t, expected, lo, hi);
    CHECK(rc == 0, "bytes recebidos integros (tamanho e soma de verificacao)");
    CHECK(t >= lo, "nao enviou mais rapido que a taxa permitida");
    CHECK(t <= hi, "nao ficou muito mais lento que a taxa permitida");

    rc = run_transfer("text/html; charset=utf-8", kbps, size, &t);
    printf("  .html: %.3f s (sem limitacao; limitado levaria ~%.2f s)\n", t, expected);
    CHECK(rc == 0, "HTML: bytes recebidos integros");
    CHECK(t < 0.5, "HTML isento: envio rapido, sem pacing");

    printf("\n== Taxa efetiva aplicada ao pacing (2 conexoes do mesmo IP)\n");
    /* 1000 kbps / 2 = 500 kbps = 62500 B/s; 125000 bytes -> ~2,0 s */
    uint32_t half = qos_effective_rate_kbps("10.9.9.9", 2);
    double exp2 = 125000.0 / 62500.0;
    double lo2 = exp2 - 8192.0 / 62500.0 - 0.05;
    double hi2 = exp2 * 1.10 + 0.10;
    rc = run_transfer("image/png", half, 125000, &t);
    printf("  .png: %.3f s (esperado ~%.2f s, aceito entre %.2f e %.2f)\n", t, exp2, lo2, hi2);
    CHECK(rc == 0 && t >= lo2 && t <= hi2, "taxa dividida por 2 conexoes respeitada");
}

int main(void)
{
    printf("Testes do modulo de QoS (Membro 1)\n");
    test_config();
    test_pacing();
    qos_free_config();

    printf("\n%s (%d falha(s))\n", g_fail ? "FALHOU" : "Todos os testes passaram.", g_fail);
    return g_fail ? 1 : 0;
}
