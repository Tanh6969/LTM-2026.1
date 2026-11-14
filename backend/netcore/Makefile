# Makefile cho netcore (tang mang viet bang C)
#   make        : bien dich cac cong cu va test vao thu muc bin/
#   make test   : chay test dong goi header (so khop voi phia Go)
#   make clean  : xoa thu muc bin/
#
# Luu y: dung API POSIX (poll, MSG_NOSIGNAL...) nen bien dich tren Linux/WSL.

CC     = gcc
CFLAGS = -Wall -Wextra -g -Iinclude -pthread

BRIDGE_SRC = src/bridge/bridge.c

all: bin/bridge_cli bin/test_bridge bin/echo_server bin/echo_client bin/lossy_proxy bin/bridge_gateway

bin:
	mkdir -p bin

bin/bridge_cli: tools/bridge_cli.c $(BRIDGE_SRC) include/bridge.h | bin
	$(CC) $(CFLAGS) -o $@ tools/bridge_cli.c $(BRIDGE_SRC)

bin/test_bridge: tests/test_bridge.c $(BRIDGE_SRC) include/bridge.h | bin
	$(CC) $(CFLAGS) -o $@ tests/test_bridge.c $(BRIDGE_SRC)

ECHO_SRC = src/protocol/echo_transport.c

bin/echo_server: src/transport/echo_server.c $(ECHO_SRC) include/echo_transport.h | bin
	$(CC) $(CFLAGS) -o $@ src/transport/echo_server.c $(ECHO_SRC)

bin/echo_client: tools/echo_client.c $(ECHO_SRC) include/echo_transport.h | bin
	$(CC) $(CFLAGS) -o $@ tools/echo_client.c $(ECHO_SRC)

bin/bridge_gateway: src/transport/bridge_gateway.c $(ECHO_SRC) $(BRIDGE_SRC) include/echo_transport.h include/bridge.h | bin
	$(CC) $(CFLAGS) -o $@ src/transport/bridge_gateway.c $(ECHO_SRC) $(BRIDGE_SRC)

bin/lossy_proxy: tools/lossy_proxy.c | bin
	$(CC) $(CFLAGS) -o $@ tools/lossy_proxy.c

test: bin/test_bridge
	./bin/test_bridge

clean:
	rm -rf bin

.PHONY: all test clean
