# Makefile cho demo TCP echo client/server
#   make        : bien dich ca server va client
#   make clean  : xoa cac file thuc thi

CC     = gcc
CFLAGS = -Wall -Wextra -g

all: tcp_server tcp_client

tcp_server: tcp_server.c
	$(CC) $(CFLAGS) -o $@ $<

tcp_client: tcp_client.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f tcp_server tcp_client

.PHONY: all clean
