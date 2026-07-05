CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -g
SRC = ../../src

.PHONY: all clean
all: test_eui64 test_topic test_registry_json
	./test_eui64
	./test_topic
	./test_registry_json

test_eui64: test_eui64.c $(SRC)/bridge_eui64.c
	$(CC) $(CFLAGS) -o $@ $^

test_topic: test_topic.c $(SRC)/bridge_topic.c $(SRC)/bridge_eui64.c
	$(CC) $(CFLAGS) -o $@ $^

test_registry_json: test_registry_json.c $(SRC)/bridge_registry_json.c
	$(CC) $(CFLAGS) $(shell pkg-config --cflags libcjson 2>/dev/null) -o $@ $^ $(shell pkg-config --libs libcjson 2>/dev/null || echo -lcjson)

clean:
	rm -f test_eui64 test_topic test_registry_json
