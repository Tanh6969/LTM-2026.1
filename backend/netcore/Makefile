# Makefile cho netcore (tang mang viet bang C)
#   make        : bien dich cac cong cu va test vao thu muc bin/
#   make test   : chay test dong goi header (so khop voi phia Go)
#   make clean  : xoa thu muc bin/
#
# Luu y: dung API POSIX (poll, MSG_NOSIGNAL...) nen bien dich tren Linux/WSL.

CC     = gcc
CFLAGS = -Wall -Wextra -g -Iinclude

BRIDGE_SRC = src/bridge/bridge.c

all: bin/bridge_cli bin/test_bridge

bin:
	mkdir -p bin

bin/bridge_cli: tools/bridge_cli.c $(BRIDGE_SRC) include/bridge.h | bin
	$(CC) $(CFLAGS) -o $@ tools/bridge_cli.c $(BRIDGE_SRC)

bin/test_bridge: tests/test_bridge.c $(BRIDGE_SRC) include/bridge.h | bin
	$(CC) $(CFLAGS) -o $@ tests/test_bridge.c $(BRIDGE_SRC)

test: bin/test_bridge
	./bin/test_bridge

clean:
	rm -rf bin

.PHONY: all test clean
