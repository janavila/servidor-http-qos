CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2 -pthread
CPPFLAGS := -Isrc
LDFLAGS := -pthread

TARGET := servidor
SOURCES := src/server.c src/http.c src/file_handler.c
OBJECTS := $(SOURCES:.c=.o)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	bash tests/run_all.sh

clean:
	rm -f $(TARGET) $(OBJECTS)
