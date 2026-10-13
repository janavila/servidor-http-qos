/** Estado global de conexoes TCP e controle atomico de admissao por IP. */
#include "qos_conn_tracker.h"
#include "qos_config.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define QOS_MAX_TRACKED_IPS 256

typedef struct {
    char ip[INET_ADDRSTRLEN];
    int count; /* 0 = entrada livre */
    uint32_t configured_rate_kbps;
} TrackEntry;

static TrackEntry g_entries[QOS_MAX_TRACKED_IPS];
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_allocated_kbps;
static uint64_t g_max_kbps;

/* Chamar com g_mutex travado. */
static TrackEntry *find_entry(const char *ip)
{
    for (int i = 0; i < QOS_MAX_TRACKED_IPS; i++)
        if (g_entries[i].count > 0 && strcmp(g_entries[i].ip, ip) == 0)
            return &g_entries[i];
    return NULL;
}

static void fill_state(const TrackEntry *entry, QosConnectionState *state)
{
    if (!state)
        return;
    state->connections_for_ip = entry ? entry->count : 0;
    state->configured_rate_kbps = entry ? entry->configured_rate_kbps : 0;
    state->effective_rate_kbps = entry && entry->count > 0
                                     ? entry->configured_rate_kbps / (uint32_t)entry->count
                                     : 0;
    if (entry && entry->count > 0 && state->effective_rate_kbps == 0)
        state->effective_rate_kbps = 1;
    state->global_allocated_kbps = g_allocated_kbps;
    state->max_global_kbps = g_max_kbps;
}

int qos_connections_init(uint64_t max_global_kbps)
{
    if (max_global_kbps == 0)
        return -1;
    pthread_mutex_lock(&g_mutex);
    memset(g_entries, 0, sizeof g_entries);
    g_allocated_kbps = 0;
    g_max_kbps = max_global_kbps;
    pthread_mutex_unlock(&g_mutex);
    return 0;
}

int qos_connection_admit(const char *ip, QosConnectionState *state)
{
    if (!ip || ip[0] == '\0')
        return -1;

    int result = -1;
    pthread_mutex_lock(&g_mutex);
    TrackEntry *e = find_entry(ip);
    uint32_t configured = qos_get_ip_limit_kbps(ip);
    uint64_t projected = g_allocated_kbps + (e ? 0u : configured);

    if (projected > g_max_kbps) {
        if (state) {
            state->connections_for_ip = e ? e->count : 0;
            state->configured_rate_kbps = configured;
            state->effective_rate_kbps = 0;
            state->global_allocated_kbps = projected;
            state->max_global_kbps = g_max_kbps;
        }
        result = 0;
        goto done;
    }

    if (!e) {
        for (int i = 0; i < QOS_MAX_TRACKED_IPS; i++) {
            if (g_entries[i].count == 0) {
                e = &g_entries[i];
                snprintf(e->ip, sizeof e->ip, "%s", ip);
                e->configured_rate_kbps = configured;
                break;
            }
        }
    }
    if (e) {
        e->count++;
        if (e->count == 1)
            g_allocated_kbps = projected;
        fill_state(e, state);
        result = 1;
    }
done:
    pthread_mutex_unlock(&g_mutex);
    return result;
}

int qos_connection_remove(const char *ip, QosConnectionState *state)
{
    if (!ip)
        return -1;
    int result = -1;
    pthread_mutex_lock(&g_mutex);
    TrackEntry *e = find_entry(ip);
    if (e) {
        e->count--;
        if (e->count == 0) {
            g_allocated_kbps -= e->configured_rate_kbps;
            e->ip[0] = '\0';
            e->configured_rate_kbps = 0;
        }
        fill_state(e->count > 0 ? e : NULL, state);
        result = 0;
    }
    pthread_mutex_unlock(&g_mutex);
    return result;
}

int qos_connection_count(const char *ip)
{
    int n = 0;
    if (!ip)
        return n;
    pthread_mutex_lock(&g_mutex);
    TrackEntry *e = find_entry(ip);
    if (e)
        n = e->count;
    pthread_mutex_unlock(&g_mutex);
    return n;
}

uint64_t qos_global_allocated_kbps(void)
{
    pthread_mutex_lock(&g_mutex);
    uint64_t value = g_allocated_kbps;
    pthread_mutex_unlock(&g_mutex);
    return value;
}

uint64_t qos_max_global_kbps(void)
{
    pthread_mutex_lock(&g_mutex);
    uint64_t value = g_max_kbps;
    pthread_mutex_unlock(&g_mutex);
    return value;
}
