#ifndef ECHO_TRANSPORT_H
#define ECHO_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#define ECHO_MAGIC 0x4C54u
#define ECHO_VERSION 1u
#define ECHO_HEADER_SIZE 32u
#define ECHO_MAX_PAYLOAD (1024u * 1024u)

enum echo_type {
    ECHO_TYPE_REQUEST = 1,
    ECHO_TYPE_RESPONSE = 2,
    ECHO_TYPE_PING = 3,
    ECHO_TYPE_PONG = 4
};

enum echo_flag {
    ECHO_FLAG_RETRANS = 1u << 0
};

typedef struct {
    uint8_t flags;
    uint16_t type;
    uint16_t app_type;
    uint32_t seq;
    uint32_t ack;
    uint64_t req_id;
    uint32_t length;
    uint32_t crc32;
} echo_header;

typedef struct {
    echo_header header;
    uint8_t *payload;
} echo_frame;

uint32_t echo_crc32(const uint8_t *data, size_t length);
int echo_send_frame(int fd, const echo_header *header, const void *payload);
/* 0=frame, 1=clean EOF, -1=protocol/socket error, -2=read timeout */
int echo_recv_frame(int fd, echo_frame *frame, int timeout_ms);
void echo_frame_free(echo_frame *frame);

#endif
