/**
 * qos_config.c - Configuração de QoS por endereço IP (MVP2, Membro 1)
 *
 * Estrutura de dados: tabela hash com endereçamento aberto (sondagem linear),
 * chave = IPv4 binário (uint32_t em ordem de rede). Guardar o IP já convertido
 * (e não a string) faz "192.168.0.10" casar independentemente de como o texto
 * foi escrito, e torna a comparação barata. Fator de carga máximo de 0,5, então
 * sempre existe um slot vazio e a busca termina.
 */
#define _POSIX_C_SOURCE 200809L /* getline, strtok_r (compilação com -std=c11) */

#include "qos_config.h"

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QOS_MIN_BYTES_PER_SEC 125u /* 1 kbps */

typedef struct {
    uint32_t key;  /* IPv4 em ordem de rede */
    uint32_t kbps; /* taxa máxima do IP */
    int used;
} QosSlot;

typedef struct {
    QosSlot *slots;
    size_t cap;   /* potência de 2, ou 0 enquanto vazia */
    size_t count; /* slots ocupados */
} QosTable;

static QosTable g_table = {NULL, 0, 0};
static pthread_rwlock_t g_lock = PTHREAD_RWLOCK_INITIALIZER;

/* ---------------------------------------------------------------- tabela */

static size_t hash_key(uint32_t k)
{
    k ^= k >> 16;
    k *= 0x7feb352dU;
    k ^= k >> 15;
    k *= 0x846ca68bU;
    k ^= k >> 16;
    return (size_t)k;
}

/* Retorna o slot da chave, ou o primeiro slot vazio da sequência de sondagem. */
static QosSlot *find_slot(QosSlot *slots, size_t cap, uint32_t key)
{
    size_t i = hash_key(key) & (cap - 1);
    while (slots[i].used && slots[i].key != key)
        i = (i + 1) & (cap - 1);
    return &slots[i];
}

static int table_grow(QosTable *t)
{
    size_t ncap = t->cap ? t->cap * 2 : 16;
    QosSlot *ns = calloc(ncap, sizeof *ns);
    if (!ns)
        return -1;
    for (size_t i = 0; i < t->cap; i++)
        if (t->slots[i].used)
            *find_slot(ns, ncap, t->slots[i].key) = t->slots[i];
    free(t->slots);
    t->slots = ns;
    t->cap = ncap;
    return 0;
}

/* Insere ou substitui. *replaced = 1 se o IP já existia. -1 se faltar memória. */
static int table_put(QosTable *t, uint32_t key, uint32_t kbps, int *replaced)
{
    if ((t->count + 1) * 2 > t->cap && table_grow(t) != 0)
        return -1;
    QosSlot *s = find_slot(t->slots, t->cap, key);
    *replaced = s->used;
    if (!s->used) {
        s->used = 1;
        s->key = key;
        t->count++;
    }
    s->kbps = kbps;
    return 0;
}

/* --------------------------------------------------------------- parsing */

/*
 * Interpreta uma linha (modifica o buffer).
 * Retorna 1 = entrada válida, 0 = linha vazia/comentário, -1 = inválida.
 */
static int parse_line(char *line, uint32_t *key, uint32_t *kbps)
{
    static const char delim[] = " \t\r\n";
    char *hash = strchr(line, '#');
    if (hash)
        *hash = '\0';

    char *save = NULL;
    char *ip_tok = strtok_r(line, delim, &save);
    if (!ip_tok)
        return 0;

    char *rate_tok = strtok_r(NULL, delim, &save);
    if (!rate_tok || strtok_r(NULL, delim, &save)) /* falta taxa ou sobrou texto */
        return -1;

    struct in_addr addr;
    if (inet_pton(AF_INET, ip_tok, &addr) != 1)
        return -1;

    if (rate_tok[0] < '0' || rate_tok[0] > '9') /* strtoul aceitaria "-5" */
        return -1;
    errno = 0;
    char *end = NULL;
    unsigned long v = strtoul(rate_tok, &end, 10);
    if (errno != 0 || *end != '\0' || v == 0 || v > QOS_MAX_RATE_KBPS)
        return -1;

    *key = addr.s_addr;
    *kbps = (uint32_t)v;
    return 1;
}

int qos_load_config(const char *filename)
{
    if (!filename) {
        errno = EINVAL;
        return -1;
    }

    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "[qos] nao foi possivel abrir '%s': %s\n", filename, strerror(errno));
        return -1;
    }

    QosTable fresh = {NULL, 0, 0};
    char *line = NULL;
    size_t line_cap = 0;
    size_t lineno = 0;
    int rc = 0;

    while (getline(&line, &line_cap, f) != -1) {
        lineno++;
        uint32_t key = 0, kbps = 0;
        int r = parse_line(line, &key, &kbps);
        if (r == 0)
            continue;
        if (r < 0) {
            fprintf(stderr, "[qos] %s:%zu: linha invalida ignorada\n", filename, lineno);
            continue;
        }
        int replaced = 0;
        if (table_put(&fresh, key, kbps, &replaced) != 0) {
            fprintf(stderr, "[qos] memoria insuficiente ao carregar '%s'\n", filename);
            rc = -1;
            break;
        }
        if (replaced)
            fprintf(stderr, "[qos] %s:%zu: IP repetido, valor anterior substituido\n", filename, lineno);
    }

    if (rc == 0 && ferror(f)) {
        fprintf(stderr, "[qos] erro de leitura em '%s'\n", filename);
        rc = -1;
    }
    free(line);
    fclose(f);

    if (rc != 0) {
        free(fresh.slots);
        return -1; /* tabela anterior permanece */
    }

    pthread_rwlock_wrlock(&g_lock);
    QosTable old = g_table;
    g_table = fresh;
    pthread_rwlock_unlock(&g_lock);
    free(old.slots);

    fprintf(stderr, "[qos] %zu entrada(s) carregada(s) de '%s'\n", fresh.count, filename);
    return 0;
}

void qos_free_config(void)
{
    pthread_rwlock_wrlock(&g_lock);
    QosTable old = g_table;
    g_table.slots = NULL;
    g_table.cap = 0;
    g_table.count = 0;
    pthread_rwlock_unlock(&g_lock);
    free(old.slots);
}

size_t qos_config_count(void)
{
    pthread_rwlock_rdlock(&g_lock);
    size_t n = g_table.count;
    pthread_rwlock_unlock(&g_lock);
    return n;
}

/* ---------------------------------------------------------------- consulta */

uint32_t qos_get_ip_limit_kbps(const char *ip_str)
{
    struct in_addr addr;
    if (!ip_str || inet_pton(AF_INET, ip_str, &addr) != 1)
        return QOS_DEFAULT_RATE_KBPS;

    uint32_t kbps = QOS_DEFAULT_RATE_KBPS;
    pthread_rwlock_rdlock(&g_lock);
    if (g_table.cap) {
        const QosSlot *s = find_slot(g_table.slots, g_table.cap, addr.s_addr);
        if (s->used)
            kbps = s->kbps;
    }
    pthread_rwlock_unlock(&g_lock);
    return kbps;
}

/* ------------------------------------------------------------ taxa efetiva */

uint64_t qos_kbps_to_bytes_per_sec(uint32_t kbps)
{
    return (uint64_t)kbps * 125u; /* kbps * 1000 / 8 */
}

uint32_t qos_effective_rate_kbps(const char *ip_str, int active_conns_for_ip)
{
    uint32_t div = active_conns_for_ip < 1 ? 1u : (uint32_t)active_conns_for_ip;
    uint32_t kbps = qos_get_ip_limit_kbps(ip_str) / div;
    return kbps ? kbps : 1u;
}

uint64_t qos_effective_bytes_per_sec(const char *ip_str, int active_conns_for_ip)
{
    uint64_t div = active_conns_for_ip < 1 ? 1u : (uint64_t)active_conns_for_ip;
    uint64_t bps = qos_kbps_to_bytes_per_sec(qos_get_ip_limit_kbps(ip_str)) / div;
    return bps < QOS_MIN_BYTES_PER_SEC ? QOS_MIN_BYTES_PER_SEC : bps;
}
