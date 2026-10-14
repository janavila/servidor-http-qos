#ifndef QOS_METRICS_H
#define QOS_METRICS_H

#include <stddef.h>
#include <time.h>

#define QOS_METRICS_MAX_CLIENTS 256

typedef struct {
    char ip[16];
    int waiting_for_object;
    int has_rtt;
    double estimated_rtt_ms;
    int has_bandwidth;
    double estimated_bandwidth_kbps;
    size_t observed_bytes;
} QosMetricsSnapshot;

int qos_metrics_init(void);
int qos_metrics_now(struct timespec *timestamp);
void qos_metrics_record_html(const char *ip, const struct timespec *arrival);
int qos_metrics_record_object(const char *ip, const struct timespec *arrival);
void qos_metrics_record_transfer(const char *ip, size_t bytes, double seconds);
int qos_metrics_get(const char *ip, QosMetricsSnapshot *snapshot);
size_t qos_metrics_snapshot(QosMetricsSnapshot *items, size_t capacity);
void qos_metrics_remove(const char *ip);

#endif
