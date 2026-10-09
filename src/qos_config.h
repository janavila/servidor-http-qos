/**
 * qos_config.h - Configuração de QoS por endereço IP (MVP2, Membro 1)
 *
 * Lê o arquivo de configuração (uma linha por cliente):
 *
 *     <endereço_IPv4> <taxa_máxima_kbps>      ex.: 192.168.0.10 2000
 *
 * Linhas em branco e comentários (do '#' até o fim da linha) são ignorados.
 * Linhas inválidas geram um aviso em stderr e são ignoradas. Se um IP aparecer
 * mais de uma vez, vale a última ocorrência.
 *
 * Convenção de unidades: 1 kbps = 1000 bits/s = 125 bytes/s.
 *
 * Thread-safety: todas as funções podem ser chamadas de qualquer thread. A
 * tabela é protegida por um pthread_rwlock_t, então qos_load_config() também
 * pode ser chamada novamente com o servidor em execução (recarga a quente).
 */
#ifndef QOS_CONFIG_H
#define QOS_CONFIG_H

#include <stddef.h>
#include <stdint.h>

/** Taxa usada quando o IP do cliente não consta no arquivo (kbps). */
#define QOS_DEFAULT_RATE_KBPS 1000u

/** Maior taxa aceita no arquivo (kbps). Valores acima são tratados como inválidos. */
#define QOS_MAX_RATE_KBPS 10000000u

/**
 * Carrega (ou recarrega) o arquivo de configuração.
 *
 * A nova tabela é montada à parte e só substitui a anterior se a leitura
 * terminar sem erro de I/O ou de memória; em caso de falha a tabela anterior
 * continua valendo.
 *
 * @return 0 em sucesso; -1 em erro (mensagem em stderr, errno preenchido
 *         quando aplicável).
 */
int qos_load_config(const char *filename);

/** Libera a tabela. Depois disso todos os IPs usam a taxa padrão. */
void qos_free_config(void);

/** Número de IPs distintos atualmente carregados. */
size_t qos_config_count(void);

/**
 * Taxa máxima do IP, em kbps.
 *
 * @param ip_str IPv4 em texto (ex.: "192.168.0.10", o mesmo formato que
 *               inet_ntop() produz). NULL ou texto inválido => taxa padrão.
 * @return taxa configurada ou QOS_DEFAULT_RATE_KBPS se o IP não constar.
 */
uint32_t qos_get_ip_limit_kbps(const char *ip_str);

/** Converte kbps para bytes por segundo (kbps * 1000 / 8). */
uint64_t qos_kbps_to_bytes_per_sec(uint32_t kbps);

/**
 * Taxa efetiva de UMA conexão, em kbps:  taxa_do_ip / conexões_ativas_do_ip.
 * active_conns_for_ip < 1 é tratado como 1. Nunca retorna menos de 1 kbps.
 */
uint32_t qos_effective_rate_kbps(const char *ip_str, int active_conns_for_ip);

/**
 * Taxa efetiva de UMA conexão, em bytes por segundo (calculada a partir da
 * taxa em bytes/s do IP, sem perder precisão pela divisão inteira em kbps).
 * active_conns_for_ip < 1 é tratado como 1. Nunca retorna menos de 125 B/s.
 */
uint64_t qos_effective_bytes_per_sec(const char *ip_str, int active_conns_for_ip);

#endif /* QOS_CONFIG_H */
