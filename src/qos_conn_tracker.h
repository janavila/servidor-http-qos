/**
 * qos_conn_tracker.h - Contagem de transferências limitadas em andamento, por IP.
 *
 * Alimenta a divisão da taxa (RF11): taxa efetiva = taxa_do_IP / transferências
 * ativas do IP. Conta apenas transferências que consomem taxa limitada (HTML não
 * entra), então conexões keep-alive ociosas não reduzem a taxa das demais.
 *
 * Se a especificação da disciplina pedir a contagem de CONEXÕES TCP abertas,
 * troque os pontos de chamada: begin() logo após o accept() e end() ao fechar a
 * conexão. A API continua a mesma.
 *
 * Thread-safe (mutex interno).
 */
#ifndef QOS_CONN_TRACKER_H
#define QOS_CONN_TRACKER_H

/**
 * Registra o início de uma transferência do IP.
 * @return 1 se registrou (o chamador DEVE chamar qos_transfer_end depois);
 *         0 se não registrou (ip NULL ou tabela cheia): não chame end.
 */
int qos_transfer_begin(const char *ip);

/** Registra o fim de uma transferência registrada por qos_transfer_begin. */
void qos_transfer_end(const char *ip);

/** Transferências ativas do IP (inclui a própria). Nunca retorna menos de 1. */
int qos_transfer_count(const char *ip);

#endif /* QOS_CONN_TRACKER_H */
