CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2 -pthread
CPPFLAGS := -Isrc -MMD -MP
LDFLAGS := -pthread

TARGET := servidor
SOURCES := src/server.c src/http.c src/file_handler.c \
           src/qos_config.c src/qos_throttling.c src/qos_conn_tracker.c
OBJECTS := $(SOURCES:.c=.o)
DEPS := $(OBJECTS:.o=.d)

QOS_TEST := tests/test_qos_member1
QOS_TEST_SOURCES := tests/test_qos_member1.c src/qos_config.c src/qos_throttling.c
MEMBER2_TEST := tests/test_qos_member2
MEMBER2_TEST_SOURCES := tests/test_qos_member2.c src/qos_config.c src/qos_conn_tracker.c

.PHONY: all clean test test-qos test-member2

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

test: $(TARGET) $(QOS_TEST) $(MEMBER2_TEST)
	./$(QOS_TEST)
	./$(MEMBER2_TEST)
	bash tests/run_all.sh
	bash tests/test_member2.sh

# Teste unitario do modulo de QoS (configuracao, taxa efetiva e pacing); nao precisa do servidor.
$(QOS_TEST): $(QOS_TEST_SOURCES)
	$(CC) -Isrc $(CFLAGS) $(QOS_TEST_SOURCES) -o $@ $(LDFLAGS)

test-qos: $(QOS_TEST)
	./$(QOS_TEST)

$(MEMBER2_TEST): $(MEMBER2_TEST_SOURCES)
	$(CC) -Isrc $(CFLAGS) $(MEMBER2_TEST_SOURCES) -o $@ $(LDFLAGS)

test-member2: $(MEMBER2_TEST) $(TARGET)
	./$(MEMBER2_TEST)
	bash tests/test_member2.sh

clean:
	rm -f $(TARGET) $(OBJECTS) $(DEPS) $(QOS_TEST) $(MEMBER2_TEST)

-include $(DEPS)
