/**
 * qos_throttling.h - Envio de arquivos com controle de taxa (MVP2, Membro 1)
 *
 * Substitui o laço "fread + send_all" do MVP1. Para arquivos HTML o envio é
 * imediato; para os demais tipos (imagens, CSS, JS...) os dados saem em blocos
 * e o servidor espera entre eles para respeitar a taxa em kbps recebida.
 */
#ifndef QOS_THROTTLING_H
#define QOS_THROTTLING_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

/**
 * Callback que informa a taxa atual (kbps) desta conexão. É consultado antes
 * de cada bloco, permitindo que a taxa mude durante a transferência quando
 * outras conexões do mesmo IP abrem ou fecham (RF11).
 */
typedef uint32_t (*qos_rate_fn)(void *ctx);

/**
 * Envia file_size bytes de `file` por `socket_fd`.
 *
 * - mime_type "text/html" (ou NULL-safe: NULL não é isento) => sem limitação.
 * - Demais tipos => taxa fixa rate_kbps_per_conn durante toda a transferência
 *   (0 é tratado como 1 kbps para não travar a conexão).
 * - `file` deve estar posicionado no início do conteúdo a enviar.
 * - O cabeçalho HTTP (Content-Length etc.) é responsabilidade do chamador.
 *
 * @return número de bytes enviados (== file_size) em sucesso; -1 em erro, com
 *         errno preenchido (EPIPE/ECONNRESET: cliente fechou; EIO: arquivo
 *         menor que file_size ou erro de leitura). Em erro o chamador deve
 *         fechar a conexão, pois o corpo ficou incompleto.
 */
ssize_t qos_send_file_throttled(int socket_fd, FILE *file, size_t file_size,
                                const char *mime_type, uint32_t rate_kbps_per_conn);

/**
 * Igual a qos_send_file_throttled(), mas a taxa vem de rate_fn(ctx), reavaliada
 * a cada bloco. Use quando o número de conexões ativas do IP pode mudar
 * durante o envio.
 */
ssize_t qos_send_file_throttled_dyn(int socket_fd, FILE *file, size_t file_size,
                                    const char *mime_type, qos_rate_fn rate_fn, void *ctx);

/** 1 se o tipo MIME é isento de limitação (text/html), 0 caso contrário. */
int qos_mime_is_exempt(const char *mime_type);

/** 1 se o caminho termina em .html ou .htm (sem diferenciar maiúsculas). */
int qos_path_is_html(const char *path);

#endif /* QOS_THROTTLING_H */
