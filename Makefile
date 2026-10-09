CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2 -pthread
# -MMD -MP: gera arquivos .d com as dependencias de headers, para que mudar um
# .h (ex.: o prototipo de serve_static_file) recompile os .c que o incluem.
CPPFLAGS := -Isrc -MMD -MP
LDFLAGS := -pthread

TARGET := servidor
SOURCES := src/server.c src/http.c src/file_handler.c \
           src/qos_config.c src/qos_throttling.c src/qos_conn_tracker.c
OBJECTS := $(SOURCES:.c=.o)
DEPS := $(OBJECTS:.o=.d)

QOS_TEST := tests/test_qos_member1
QOS_TEST_SOURCES := tests/test_qos_member1.c src/qos_config.c src/qos_throttling.c

.PHONY: all clean test test-qos

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	bash tests/run_all.sh

# Teste unitario do modulo de QoS (configuracao, taxa efetiva e pacing); nao precisa do servidor.
$(QOS_TEST): $(QOS_TEST_SOURCES)
	$(CC) -Isrc $(CFLAGS) $(QOS_TEST_SOURCES) -o $@ $(LDFLAGS)

test-qos: $(QOS_TEST)
	./$(QOS_TEST)

clean:
	rm -f $(TARGET) $(OBJECTS) $(DEPS) $(QOS_TEST)

-include $(DEPS)
