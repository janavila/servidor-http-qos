#define _POSIX_C_SOURCE 200809L

#include "qos_metrics.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int used;
    char ip[16];
    int waiting_for_object;
    struct timespec html_arrival;
    int has_rtt;
    double estimated_rtt_ms;
    int has_bandwidth;
    double estimated_bandwidth_kbps;
    size_t observed_bytes;
} MetricsEntry;

static MetricsEntry g_entries[QOS_METRICS_MAX_CLIENTS];
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

static MetricsEntry *find_entry(const char *ip)
{
    for (size_t i = 0; i < QOS_METRICS_MAX_CLIENTS; i++)
        if (g_entries[i].used && strcmp(g_entries[i].ip, ip) == 0)
            return &g_entries[i];
    return NULL;
}

static MetricsEntry *find_or_create(const char *ip)
{
    MetricsEntry *entry = find_entry(ip);
    if (entry)
        return entry;
    for (size_t i = 0; i < QOS_METRICS_MAX_CLIENTS; i++) {
        if (!g_entries[i].used) {
            g_entries[i].used = 1;
            snprintf(g_entries[i].ip, sizeof g_entries[i].ip, "%s", ip);
            return &g_entries[i];
        }
    }
    return NULL;
}

static double elapsed_ms(const struct timespec *start, const struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) * 1000.0 +
           (double)(end->tv_nsec - start->tv_nsec) / 1000000.0;
}

static void copy_snapshot(const MetricsEntry *entry, QosMetricsSnapshot *snapshot)
{
    memset(snapshot, 0, sizeof *snapshot);
    snprintf(snapshot->ip, sizeof snapshot->ip, "%s", entry->ip);
    snapshot->waiting_for_object = entry->waiting_for_object;
    snapshot->has_rtt = entry->has_rtt;
    snapshot->estimated_rtt_ms = entry->estimated_rtt_ms;
    snapshot->has_bandwidth = entry->has_bandwidth;
    snapshot->estimated_bandwidth_kbps = entry->estimated_bandwidth_kbps;
    snapshot->observed_bytes = entry->observed_bytes;
}

int qos_metrics_init(void)
{
    pthread_mutex_lock(&g_mutex);
    memset(g_entries, 0, sizeof g_entries);
    pthread_mutex_unlock(&g_mutex);
    return 0;
}

int qos_metrics_now(struct timespec *timestamp)
{
    return timestamp ? clock_gettime(CLOCK_MONOTONIC, timestamp) : -1;
}

void qos_metrics_record_html(const char *ip, const struct timespec *arrival)
{
    if (!ip || !arrival)
        return;
    pthread_mutex_lock(&g_mutex);
    MetricsEntry *entry = find_or_create(ip);
    if (entry) {
        entry->html_arrival = *arrival;
        entry->waiting_for_object = 1;
    }
    pthread_mutex_unlock(&g_mutex);
}

int qos_metrics_record_object(const char *ip, const struct timespec *arrival)
{
    int completed = 0;
    if (!ip || !arrival)
        return 0;
    pthread_mutex_lock(&g_mutex);
    MetricsEntry *entry = find_entry(ip);
    if (entry && entry->waiting_for_object) {
        double interval = elapsed_ms(&entry->html_arrival, arrival);
        if (interval >= 0.0) {
            /* Estimativa de RTT em nivel de aplicacao: T2-T1. Ela inclui envio
             * do HTML e processamento do cliente, portanto e um limite superior
             * e nao substitui uma medicao TCP/ICMP de RTT. */
            entry->estimated_rtt_ms = interval;
            entry->has_rtt = 1;
            entry->waiting_for_object = 0;
            completed = 1;
        }
    }
    pthread_mutex_unlock(&g_mutex);
    return completed;
}

void qos_metrics_record_transfer(const char *ip, size_t bytes, double seconds)
{
    if (!ip || seconds <= 0.0)
        return;
    pthread_mutex_lock(&g_mutex);
    MetricsEntry *entry = find_or_create(ip);
    if (entry) {
        entry->estimated_bandwidth_kbps = ((double)bytes * 8.0) / (seconds * 1000.0);
        entry->observed_bytes = bytes;
        entry->has_bandwidth = 1;
    }
    pthread_mutex_unlock(&g_mutex);
}

int qos_metrics_get(const char *ip, QosMetricsSnapshot *snapshot)
{
    int found = 0;
    if (!ip || !snapshot)
        return 0;
    pthread_mutex_lock(&g_mutex);
    MetricsEntry *entry = find_entry(ip);
    if (entry) {
        copy_snapshot(entry, snapshot);
        found = 1;
    }
    pthread_mutex_unlock(&g_mutex);
    return found;
}

size_t qos_metrics_snapshot(QosMetricsSnapshot *items, size_t capacity)
{
    size_t count = 0;
    if (!items || capacity == 0)
        return 0;
    pthread_mutex_lock(&g_mutex);
    for (size_t i = 0; i < QOS_METRICS_MAX_CLIENTS && count < capacity; i++)
        if (g_entries[i].used)
            copy_snapshot(&g_entries[i], &items[count++]);
    pthread_mutex_unlock(&g_mutex);
    return count;
}

void qos_metrics_remove(const char *ip)
{
    if (!ip)
        return;
    pthread_mutex_lock(&g_mutex);
    MetricsEntry *entry = find_entry(ip);
    if (entry)
        memset(entry, 0, sizeof *entry);
    pthread_mutex_unlock(&g_mutex);
}
