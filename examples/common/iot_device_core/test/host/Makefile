CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -g
SRC = ../../src
CJSON_CFLAGS = $(shell pkg-config --cflags libcjson 2>/dev/null)
CJSON_LIBS = $(shell pkg-config --libs libcjson 2>/dev/null || echo -lcjson)

.PHONY: all clean
all: test_envelope test_dispatch
	./test_envelope
	./test_dispatch

test_envelope: test_envelope.c $(SRC)/iot_envelope.c
	$(CC) $(CFLAGS) $(CJSON_CFLAGS) -o $@ $^ $(CJSON_LIBS)

test_dispatch: test_dispatch.c $(SRC)/iot_dispatch.c
	$(CC) $(CFLAGS) $(CJSON_CFLAGS) -I../../include -o $@ $^ $(CJSON_LIBS)

clean:
	rm -f test_envelope test_dispatch