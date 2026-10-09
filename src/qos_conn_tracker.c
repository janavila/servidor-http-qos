/**
 * qos_conn_tracker.c - Contagem de transferências limitadas em andamento, por IP.
 *
 * Tabela pequena e fixa: até QOS_MAX_TRACKED_IPS endereços com transferência em
 * andamento ao mesmo tempo. A entrada de um IP é liberada quando sua contagem
 * volta a zero. Busca linear, suficiente para esse tamanho e para o número de
 * clientes dos experimentos.
 */
#include "qos_conn_tracker.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define QOS_MAX_TRACKED_IPS 256

typedef struct {
    char ip[INET_ADDRSTRLEN];
    int count; /* 0 = entrada livre */
} TrackEntry;

static TrackEntry g_entries[QOS_MAX_TRACKED_IPS];
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Chamar com g_mutex travado. */
static TrackEntry *find_entry(const char *ip)
{
    for (int i = 0; i < QOS_MAX_TRACKED_IPS; i++)
        if (g_entries[i].count > 0 && strcmp(g_entries[i].ip, ip) == 0)
            return &g_entries[i];
    return NULL;
}

int qos_transfer_begin(const char *ip)
{
    if (!ip)
        return 0;

    int registered = 0;
    pthread_mutex_lock(&g_mutex);
    TrackEntry *e = find_entry(ip);
    if (!e) {
        for (int i = 0; i < QOS_MAX_TRACKED_IPS; i++) {
            if (g_entries[i].count == 0) {
                e = &g_entries[i];
                snprintf(e->ip, sizeof e->ip, "%s", ip);
                break;
            }
        }
    }
    if (e) {
        e->count++;
        registered = 1;
    }
    pthread_mutex_unlock(&g_mutex);
    return registered;
}

void qos_transfer_end(const char *ip)
{
    if (!ip)
        return;
    pthread_mutex_lock(&g_mutex);
    TrackEntry *e = find_entry(ip);
    if (e)
        e->count--;
    pthread_mutex_unlock(&g_mutex);
}

int qos_transfer_count(const char *ip)
{
    int n = 1;
    if (!ip)
        return n;
    pthread_mutex_lock(&g_mutex);
    TrackEntry *e = find_entry(ip);
    if (e && e->count > 1)
        n = e->count;
    pthread_mutex_unlock(&g_mutex);
    return n;
}
