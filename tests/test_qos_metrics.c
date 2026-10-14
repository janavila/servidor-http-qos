#define _POSIX_C_SOURCE 200809L

#include "qos_metrics.h"

#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(condition, message) do { \
    if (condition) printf("  [ OK  ] %s\n", message); \
    else { printf("  [FALHA] %s (linha %d)\n", message, __LINE__); failures++; } \
} while (0)

int main(void)
{
    const char *ip = "127.0.0.1";
    struct timespec html = {100, 100000000};
    struct timespec object = {100, 125000000};
    QosMetricsSnapshot metric;
    QosMetricsSnapshot all[4];

    qos_metrics_init();
    CHECK(qos_metrics_get(ip, &metric) == 0, "IP inicialmente sem metricas");
    qos_metrics_record_html(ip, &html);
    CHECK(qos_metrics_get(ip, &metric) == 1 && metric.waiting_for_object,
          "HTML deixa medicao aguardando primeiro objeto");
    CHECK(qos_metrics_record_object(ip, &object) == 1,
          "primeiro objeto conclui a estimativa de RTT");
    CHECK(qos_metrics_get(ip, &metric) == 1 && metric.has_rtt &&
          fabs(metric.estimated_rtt_ms - 25.0) < 0.001,
          "RTT estimado usa T2-T1 em milissegundos");
    CHECK(qos_metrics_record_object(ip, &object) == 0,
          "objetos seguintes nao sobrescrevem o RTT sem novo HTML");

    qos_metrics_record_transfer(ip, 125000, 1.0);
    CHECK(qos_metrics_get(ip, &metric) == 1 && metric.has_bandwidth &&
          fabs(metric.estimated_bandwidth_kbps - 1000.0) < 0.001,
          "banda estimada usa bytes*8/(segundos*1000)");
    CHECK(qos_metrics_snapshot(all, 4) == 1, "snapshot retorna cliente medido");
    qos_metrics_remove(ip);
    CHECK(qos_metrics_get(ip, &metric) == 0, "estado removido sem cliente ativo");

    printf("%s (%d falha(s))\n", failures ? "FALHOU" : "Metricas passaram.", failures);
    return failures ? 1 : 0;
}
