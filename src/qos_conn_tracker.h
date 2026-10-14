/** Estado global de conexoes TCP e controle de admissao (thread-safe). */
#ifndef QOS_CONN_TRACKER_H
#define QOS_CONN_TRACKER_H

#include <stdint.h>
#include <stddef.h>

#define QOS_CONNECTIONS_MAX_IPS 256

typedef struct {
    int connections_for_ip;
    uint32_t configured_rate_kbps;
    uint32_t effective_rate_kbps;
    uint64_t global_allocated_kbps;
    uint64_t max_global_kbps;
} QosConnectionState;

typedef struct {
    char ip[16];
    int connections_for_ip;
    uint32_t configured_rate_kbps;
    uint32_t effective_rate_kbps;
} QosConnectionSnapshot;

/** Configura a capacidade e zera o estado antes da criacao das threads. */
int qos_connections_init(uint64_t max_global_kbps);
/** Admite atomicamente: 1=admitida, 0=capacidade excedida, -1=erro. */
int qos_connection_admit(const char *ip, QosConnectionState *state);

/** Remove um socket TCP anteriormente admitido. */
int qos_connection_remove(const char *ip, QosConnectionState *state);

/** Numero real de sockets TCP ativos do IP; zero se nao houver. */
int qos_connection_count(const char *ip);
uint64_t qos_global_allocated_kbps(void);
uint64_t qos_max_global_kbps(void);

/** Copia o estado ativo sob mutex; a impressao pode ocorrer sem manter o lock. */
size_t qos_connections_snapshot(QosConnectionSnapshot *items, size_t capacity,
                                uint64_t *allocated_kbps, uint64_t *max_kbps);

#endif /* QOS_CONN_TRACKER_H */
