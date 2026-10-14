#define _POSIX_C_SOURCE 200809L

#include "qos_config.h"
#include "qos_conn_tracker.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int failures;

#define CHECK(condition, message)                                      \
    do {                                                               \
        if (condition) printf("  [ OK  ] %s\n", message);              \
        else {                                                         \
            printf("  [FALHA] %s (linha %d)\n", message, __LINE__);   \
            failures++;                                                \
        }                                                              \
    } while (0)

static void test_admission(void)
{
    QosConnectionState state;
    QosConnectionSnapshot snapshot[4];
    uint64_t allocated = 0, maximum = 0;

    CHECK(qos_connections_init(2000) == 0, "estado inicializado");
    CHECK(qos_connection_admit("10.0.0.1", &state) == 1,
          "primeira conexao do IP admitida");
    CHECK(state.connections_for_ip == 1 && state.effective_rate_kbps == 1000,
          "primeira conexao recebe a taxa completa");
    CHECK(state.global_allocated_kbps == 1000, "vazao global do primeiro IP");

    CHECK(qos_connection_admit("10.0.0.1", &state) == 1,
          "segunda conexao do mesmo IP admitida");
    CHECK(qos_connection_count("10.0.0.1") == 2,
          "duas conexoes TCP contabilizadas");
    CHECK(state.effective_rate_kbps == 500 && state.global_allocated_kbps == 1000,
          "taxa dividida sem dupla contabilizacao global");
    CHECK(qos_connections_snapshot(snapshot, 4, &allocated, &maximum) == 1 &&
          snapshot[0].connections_for_ip == 2 &&
          snapshot[0].effective_rate_kbps == 500 && allocated == 1000 && maximum == 2000,
          "snapshot seguro reflete conexoes e vazao atuais");

    CHECK(qos_connection_remove("10.0.0.1", &state) == 0,
          "uma conexao removida");
    CHECK(state.connections_for_ip == 1 && state.effective_rate_kbps == 1000,
          "conexao restante volta a taxa completa");

    CHECK(qos_connection_admit("10.0.0.2", &state) == 0,
          "segundo IP rejeitado ao exceder capacidade");
    CHECK(state.global_allocated_kbps == 2500 && qos_global_allocated_kbps() == 1000,
          "rejeicao informa projecao sem alterar estado");

    CHECK(qos_connection_remove("10.0.0.1", &state) == 0,
          "ultima conexao do primeiro IP removida");
    CHECK(qos_connection_count("10.0.0.1") == 0 &&
          qos_global_allocated_kbps() == 0,
          "IP sem conexoes libera sua alocacao");
    CHECK(qos_connection_admit("10.0.0.2", &state) == 1,
          "capacidade liberada permite o segundo IP");
    CHECK(state.global_allocated_kbps == 1500, "segundo IP aloca 1500 kbps");
    CHECK(qos_connection_remove("10.0.0.2", NULL) == 0, "limpeza do segundo IP");

    CHECK(qos_connections_init(5000) == 0, "estado reinicializado");
    CHECK(qos_connection_admit("10.0.0.1", NULL) == 1, "IP A admitido");
    CHECK(qos_connection_admit("10.0.0.2", &state) == 1, "IP B admitido");
    CHECK(state.global_allocated_kbps == 2500, "vazao global soma IPs distintos");
    qos_connection_remove("10.0.0.1", NULL);
    qos_connection_remove("10.0.0.2", NULL);
}

typedef struct {
    int iterations;
    int errors;
} Worker;

static void *worker_main(void *argument)
{
    Worker *worker = argument;
    for (int i = 0; i < worker->iterations; i++) {
        if (qos_connection_admit("192.168.50.10", NULL) != 1) {
            worker->errors++;
            continue;
        }
        if (qos_connection_remove("192.168.50.10", NULL) != 0)
            worker->errors++;
    }
    return NULL;
}

static void test_concurrency(void)
{
    enum { THREADS = 8, ITERATIONS = 1000 };
    pthread_t threads[THREADS];
    Worker workers[THREADS] = {{0}};
    int created[THREADS] = {0};

    CHECK(qos_connections_init(10000) == 0, "estado concorrente inicializado");
    for (int i = 0; i < THREADS; i++) {
        workers[i].iterations = ITERATIONS;
        created[i] = pthread_create(&threads[i], NULL, worker_main, &workers[i]) == 0;
        CHECK(created[i], "thread de teste criada");
    }
    for (int i = 0; i < THREADS; i++)
        if (created[i])
            pthread_join(threads[i], NULL);

    int errors = 0;
    for (int i = 0; i < THREADS; i++)
        errors += workers[i].errors;
    CHECK(errors == 0, "registro/remocao concorrente sem falhas");
    CHECK(qos_connection_count("192.168.50.10") == 0,
          "contador final nao foi corrompido");
    CHECK(qos_global_allocated_kbps() == 0,
          "vazao global final nao foi corrompida");
}

int main(void)
{
    char path[] = "/tmp/qos_member2_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 2;
    FILE *file = fdopen(fd, "w");
    if (!file) {
        close(fd);
        unlink(path);
        return 2;
    }
    fputs("10.0.0.1 1000\n10.0.0.2 1500\n", file);
    fclose(file);
    if (qos_load_config(path) != 0) {
        unlink(path);
        return 2;
    }
    unlink(path);

    printf("Testes do modulo de QoS (Membro 2)\n");
    test_admission();
    test_concurrency();
    qos_free_config();
    printf("\n%s (%d falha(s))\n",
           failures ? "FALHOU" : "Todos os testes passaram.", failures);
    return failures ? 1 : 0;
}
