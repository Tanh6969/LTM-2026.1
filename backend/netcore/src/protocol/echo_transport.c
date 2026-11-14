#include "echo_transport.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }
static void put64(uint8_t *p, uint64_t v) { put32(p, (uint32_t)(v >> 32)); put32(p + 4, (uint32_t)v); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static uint32_t get32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static uint64_t get64(const uint8_t *p) { return ((uint64_t)get32(p)<<32)|get32(p+4); }

uint32_t echo_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return crc ^ 0xFFFFFFFFu;
}

static void encode(const echo_header *h, uint8_t out[ECHO_HEADER_SIZE], uint32_t crc) {
    memset(out, 0, ECHO_HEADER_SIZE);
    put16(out, ECHO_MAGIC); out[2] = ECHO_VERSION; out[3] = h->flags;
    put16(out + 4, h->type); put16(out + 6, h->app_type); put32(out + 8, h->seq); put32(out + 12, h->ack);
    put32(out + 16, h->length); put32(out + 20, crc); put64(out + 24, h->req_id);
}

static int send_all(int fd, const uint8_t *data, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        ssize_t n = send(fd, data + sent, length - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int recv_exact(int fd, uint8_t *data, size_t length, int timeout_ms, int allow_clean_eof) {
    size_t got = 0;
    while (got < length) {
        struct pollfd pfd = {.fd = fd, .events = POLLIN};
        int ready;
        do { ready = poll(&pfd, 1, timeout_ms); } while (ready < 0 && errno == EINTR);
        if (ready == 0) return -2;
        if (ready < 0) return -1;
        ssize_t n = recv(fd, data + got, length - got, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return -1;
        if (n == 0) return (got == 0 && allow_clean_eof) ? 1 : -1;
        got += (size_t)n;
        /* timeout applies to the whole frame, not separately to every fragment */
        timeout_ms = 5000;
    }
    return 0;
}

int echo_send_frame(int fd, const echo_header *input, const void *payload) {
    if (!input || input->length > ECHO_MAX_PAYLOAD || (input->length && !payload)) return -1;
    echo_header h = *input;
    uint8_t raw[ECHO_HEADER_SIZE];
    encode(&h, raw, 0);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < ECHO_HEADER_SIZE; ++i) {
        uint8_t byte = (i >= 20 && i < 24) ? 0 : raw[i];
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    const uint8_t *body = payload;
    for (uint32_t i = 0; i < h.length; ++i) {
        crc ^= body[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    encode(&h, raw, crc ^ 0xFFFFFFFFu);
    if (send_all(fd, raw, sizeof(raw)) < 0) return -1;
    return h.length ? send_all(fd, body, h.length) : 0;
}

int echo_recv_frame(int fd, echo_frame *frame, int timeout_ms) {
    uint8_t raw[ECHO_HEADER_SIZE];
    memset(frame, 0, sizeof(*frame));
    int rc = recv_exact(fd, raw, sizeof(raw), timeout_ms, 1);
    if (rc != 0) return rc;
    if (get16(raw) != ECHO_MAGIC || raw[2] != ECHO_VERSION || get32(raw + 16) > ECHO_MAX_PAYLOAD) return -1;
    frame->header.flags = raw[3]; frame->header.type = get16(raw + 4); frame->header.app_type = get16(raw + 6); frame->header.crc32 = get32(raw + 20);
    frame->header.seq = get32(raw + 8); frame->header.ack = get32(raw + 12); frame->header.length = get32(raw + 16); frame->header.req_id = get64(raw + 24);
    if (frame->header.length) {
        frame->payload = malloc(frame->header.length);
        if (!frame->payload) return -1;
        rc = recv_exact(fd, frame->payload, frame->header.length, timeout_ms, 0);
        if (rc != 0) { echo_frame_free(frame); return rc; }
    }
    uint32_t expected = frame->header.crc32;
    encode(&frame->header, raw, 0);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < ECHO_HEADER_SIZE; ++i) {
        uint8_t byte = (i >= 20 && i < 24) ? 0 : raw[i];
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    for (uint32_t i = 0; i < frame->header.length; ++i) {
        crc ^= frame->payload[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    if ((crc ^ 0xFFFFFFFFu) != expected) { echo_frame_free(frame); return -1; }
    return 0;
}

void echo_frame_free(echo_frame *frame) { free(frame->payload); frame->payload = NULL; }
