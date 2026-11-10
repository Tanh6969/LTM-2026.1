/*
 * bridge.h - Giao thuc "bridge" giua netcore (C) va server nghiep vu (Go)
 *
 * Moi thong diep tren duong noi bo la mot KHUNG (frame) gom:
 *   [ header 24 byte ][ payload JSON UTF-8, "length" byte ]
 *
 * TCP chi la dong byte, khong co ranh gioi thong diep, nen header co truong
 * "length" de ben nhan biet can doc bao nhieu byte tiep theo.
 *
 * Cau truc header (big-endian = byte cao truoc, con goi la "thu tu byte mang"):
 *
 *   offset  size  truong     y nghia
 *   ------  ----  ---------  ---------------------------------------------
 *      0     2    magic      0x4C54 ("LT"), de nhan ra khung hop le
 *      2     1    version    phien ban giao thuc (hien la 1)
 *      3     1    kind       REQUEST / RESPONSE / PUSH / ...
 *      4     2    type       loai thong diep nghiep vu (AUTH_LOGIN, ...)
 *      6     2    status     ma ket qua (0 = OK), chi co nghia trong RESPONSE
 *      8     4    conn_id    ket noi cua client phia netcore
 *     12     8    req_id     so yeu cau, ben nhan tra lai nguyen ven
 *     20     4    length     so byte payload
 *
 * Dac ta day du: docs/BRIDGE-PROTOCOL.md. Phia Go: internal/infra/bridge/protocol.go.
 */

#ifndef BRIDGE_H
#define BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#define BRIDGE_MAGIC        0x4C54
#define BRIDGE_VERSION      1
#define BRIDGE_HEADER_SIZE  24
#define BRIDGE_MAX_PAYLOAD  (1024 * 1024)   /* 1 MiB, phai khop voi phia Go */

/* Vai tro cua khung */
enum bridge_kind {
    BRIDGE_REQUEST    = 1,  /* C -> Go: yeu cau nghiep vu cua mot client */
    BRIDGE_RESPONSE   = 2,  /* Go -> C: phan hoi cho mot REQUEST */
    BRIDGE_PUSH       = 3,  /* Go -> C: tin day chu dong (conn_id = 0: gui moi ket noi) */
    BRIDGE_CONN_OPEN  = 4,  /* C -> Go: co client moi ket noi */
    BRIDGE_CONN_CLOSE = 5,  /* C -> Go: client da ngat */
    BRIDGE_PING       = 6,  /* kiem tra duong noi con song */
    BRIDGE_PONG       = 7,  /* tra loi PING, giu nguyen payload */
    BRIDGE_KICK       = 8   /* Go -> C: yeu cau dong ket noi conn_id */
};

/* Ma ket qua trong RESPONSE (khop voi phia Go) */
enum bridge_status {
    BRIDGE_OK               = 0,
    BRIDGE_ERR_BAD_REQUEST  = 400,
    BRIDGE_ERR_UNAUTHORIZED = 401,
    BRIDGE_ERR_FORBIDDEN    = 403,
    BRIDGE_ERR_NOT_FOUND    = 404,
    BRIDGE_ERR_CONFLICT     = 409,
    BRIDGE_ERR_INTERNAL     = 500,
    BRIDGE_ERR_UNKNOWN_TYPE = 501,
    BRIDGE_ERR_UNAVAILABLE  = 503,
    BRIDGE_ERR_TIMEOUT      = 504
};

/* Loai thong diep nghiep vu (khop voi internal/infra/bridge/types.go) */
enum bridge_msg_type {
    MSG_SYS_HEALTH     = 0x0001,
    MSG_AUTH_REGISTER  = 0x0101,
    MSG_AUTH_LOGIN     = 0x0102,
    MSG_AUTH_LOGOUT    = 0x0103,
    MSG_AUTH_TOKEN     = 0x0104,
    MSG_FLIGHT_SEARCH  = 0x0201,
    MSG_FLIGHT_GET     = 0x0202,
    MSG_FLIGHT_LIST    = 0x0203
};

/* Header da giai ma (dang so nguyen cua may, khong con la byte mang) */
typedef struct {
    uint8_t  kind;
    uint16_t type;
    uint16_t status;
    uint32_t conn_id;
    uint64_t req_id;
    uint32_t length;
} bridge_header;

/* Mot khung day du. payload do malloc, luon co them '\0' o cuoi de in nhu chuoi;
 * payload = NULL khi length = 0. Giai phong bang bridge_frame_free(). */
typedef struct {
    bridge_header hdr;
    char         *payload;
} bridge_frame;

/* Ma tra ve cua bridge_decode_header */
#define BRIDGE_DECODE_OK            0
#define BRIDGE_DECODE_BAD_MAGIC    -1
#define BRIDGE_DECODE_BAD_VERSION  -2
#define BRIDGE_DECODE_TOO_LARGE    -3

void bridge_encode_header(const bridge_header *h, uint8_t out[BRIDGE_HEADER_SIZE]);
int  bridge_decode_header(const uint8_t in[BRIDGE_HEADER_SIZE], bridge_header *h);

int  bridge_connect(const char *ip, int port);
int  bridge_send_frame(int fd, const bridge_header *h, const char *payload, size_t len);
int  bridge_recv_frame(int fd, bridge_frame *f);
void bridge_frame_free(bridge_frame *f);

int  bridge_wait_readable(int fd, int timeout_ms);

const char *bridge_kind_name(uint8_t kind);
const char *bridge_type_name(uint16_t type);

#endif /* BRIDGE_H */
