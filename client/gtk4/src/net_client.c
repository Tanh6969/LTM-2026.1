#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "net_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define BAD_SOCKET INVALID_SOCKET
#define close_socket closesocket
#else
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>
#include <unistd.h>
typedef int socket_t;
#define BAD_SOCKET (-1)
#define close_socket close
#endif

#define HEADER_SIZE 32u
#define MAX_PAYLOAD (1024u * 1024u)
#define MAGIC 0x4c54u
#define VERSION 1u
#define KIND_REQUEST 1u
#define KIND_RESPONSE 2u
#define KIND_PING 3u
#define KIND_PONG 4u

struct NetClient {
    char *host;
    uint16_t port;
    socket_t fd;
    uint32_t seq;
    uint64_t next_id;
#ifdef _WIN32
    int winsock_ready;
#endif
};

static char *copy_text(const char *s) {
    size_t n = strlen(s) + 1;
    char *out = malloc(n);
    if (out) memcpy(out, s, n);
    return out;
}

static void put16(unsigned char *p, uint16_t v) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }
static void put32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}
static void put64(unsigned char *p, uint64_t v) { put32(p, (uint32_t)(v >> 32)); put32(p + 4, (uint32_t)v); }
static uint16_t get16(const unsigned char *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static uint32_t get32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint64_t get64(const unsigned char *p) { return ((uint64_t)get32(p) << 32) | get32(p + 4); }

static uint32_t crc32_update(uint32_t crc, const unsigned char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (unsigned j = 0; j < 8; j++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return crc;
}

static void disconnect_client(NetClient *c) {
    if (c->fd != BAD_SOCKET) { close_socket(c->fd); c->fd = BAD_SOCKET; }
}

static int connect_client(NetClient *c) {
    if (c->fd != BAD_SOCKET) return 0;
    char port[8];
    snprintf(port, sizeof port, "%u", c->port);
    struct addrinfo hints = {0}, *list = NULL, *it;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(c->host, port, &hints, &list) != 0) return -1;
    for (it = list; it; it = it->ai_next) {
        socket_t fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd == BAD_SOCKET) continue;
#ifdef _WIN32
        DWORD timeout = 5000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof timeout);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof timeout);
#else
        struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
#endif
        if (connect(fd, it->ai_addr, (int)it->ai_addrlen) == 0) { c->fd = fd; break; }
        close_socket(fd);
    }
    freeaddrinfo(list);
    return c->fd == BAD_SOCKET ? -1 : 0;
}

static int write_all(socket_t fd, const unsigned char *p, size_t n) {
    while (n) {
        int chunk = n > 65536 ? 65536 : (int)n;
        int sent = send(fd, (const char *)p, chunk, 0);
        if (sent <= 0) return -1;
        p += sent; n -= (size_t)sent;
    }
    return 0;
}

static int read_all(socket_t fd, unsigned char *p, size_t n) {
    while (n) {
        int chunk = n > 65536 ? 65536 : (int)n;
        int got = recv(fd, (char *)p, chunk, 0);
        if (got <= 0) return -1;
        p += got; n -= (size_t)got;
    }
    return 0;
}

static int send_frame(NetClient *c, uint16_t kind, uint16_t type, uint64_t id,
                      uint32_t seq, const char *body, size_t len) {
    if (len > MAX_PAYLOAD) return -1;
    unsigned char header[HEADER_SIZE] = {0};
    put16(header, MAGIC); header[2] = VERSION;
    put16(header + 4, kind); put16(header + 6, type);
    put32(header + 8, seq); put32(header + 16, (uint32_t)len);
    put64(header + 24, id);
    uint32_t crc = crc32_update(0xffffffffu, header, HEADER_SIZE);
    crc = crc32_update(crc, (const unsigned char *)body, len) ^ 0xffffffffu;
    put32(header + 20, crc);
    if (write_all(c->fd, header, HEADER_SIZE) != 0) return -1;
    return len ? write_all(c->fd, (const unsigned char *)body, len) : 0;
}

static int read_frame(NetClient *c, uint16_t *kind, uint16_t *type, uint64_t *id,
                      uint32_t *seq, char **body) {
    unsigned char header[HEADER_SIZE];
    if (read_all(c->fd, header, HEADER_SIZE) != 0) return -1;
    uint32_t len = get32(header + 16);
    if (get16(header) != MAGIC || header[2] != VERSION || len > MAX_PAYLOAD) return -1;
    uint32_t expected = get32(header + 20);
    memset(header + 20, 0, 4);
    char *payload = malloc((size_t)len + 1);
    if (!payload) return -1;
    if (len && read_all(c->fd, (unsigned char *)payload, len) != 0) { free(payload); return -1; }
    payload[len] = 0;
    uint32_t crc = crc32_update(0xffffffffu, header, HEADER_SIZE);
    crc = crc32_update(crc, (const unsigned char *)payload, len) ^ 0xffffffffu;
    if (crc != expected) { free(payload); return -1; }
    *kind = get16(header + 4); *type = get16(header + 6);
    *seq = get32(header + 8); *id = get64(header + 24); *body = payload;
    return 0;
}

NetClient *net_client_new(const char *host, uint16_t port) {
    NetClient *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->host = copy_text(host); c->port = port; c->fd = BAD_SOCKET;
    c->seq = 1; c->next_id = 1;
#ifdef _WIN32
    WSADATA data;
    c->winsock_ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    if (!c->winsock_ready) { net_client_free(c); return NULL; }
#endif
    if (!c->host) { net_client_free(c); return NULL; }
    return c;
}

void net_client_free(NetClient *c) {
    if (!c) return;
    disconnect_client(c);
#ifdef _WIN32
    if (c->winsock_ready) WSACleanup();
#endif
    free(c->host); free(c);
}

NetReply net_client_request(NetClient *c, uint16_t type, const char *json) {
    NetReply reply = {0};
    if (!c || !json || strlen(json) > MAX_PAYLOAD) {
        reply.error = copy_text("Yêu cầu không hợp lệ hoặc quá lớn."); return reply;
    }
    if (connect_client(c) != 0) {
        reply.error = copy_text("Không kết nối được TCP server."); return reply;
    }
    uint64_t id = c->next_id++;
    if (send_frame(c, KIND_REQUEST, type, id, c->seq++, json, strlen(json)) != 0) goto network_error;
    for (;;) {
        uint16_t kind, response_type;
        uint64_t response_id;
        uint32_t seq;
        char *body = NULL;
        if (read_frame(c, &kind, &response_type, &response_id, &seq, &body) != 0) goto network_error;
        if (kind == KIND_PING) {
            int rc = send_frame(c, KIND_PONG, 0, 0, seq, NULL, 0);
            free(body);
            if (rc != 0) goto network_error;
            continue;
        }
        if (kind == KIND_RESPONSE && response_id == id) {
            reply.status = response_type;
            reply.body = body;
            return reply;
        }
        free(body);
    }
network_error:
    disconnect_client(c);
    reply.error = copy_text("Kết nối bị ngắt hoặc phản hồi TCP không hợp lệ.");
    return reply;
}

void net_reply_clear(NetReply *reply) {
    if (!reply) return;
    free(reply->body); free(reply->error);
    *reply = (NetReply){0};
}
