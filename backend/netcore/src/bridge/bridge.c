/*
 * bridge.c - Doc/ghi khung tren duong noi bo netcore <-> Go
 *
 * Chuc nang: dong goi va giai ma header, gui/nhan DUNG MOT khung tren socket TCP.
 *
 * Diem mau chot cua file nay (TCP la dong byte):
 *   - send() co the chi gui DUOC MOT PHAN du lieu  -> phai lap cho den khi gui het.
 *   - recv() co the tra ve it hon so byte can doc  -> phai lap cho den khi du.
 * Hai ham send_all / recv_all ben duoi xu ly viec do.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>      /* close() */
#include <poll.h>        /* poll() */
#include <sys/types.h>
#include <sys/socket.h>  /* socket, connect, send, recv */
#include <netinet/in.h>  /* struct sockaddr_in, htons */
#include <netinet/tcp.h> /* TCP_NODELAY */
#include <arpa/inet.h>   /* inet_pton */

#include "bridge.h"

/* ---------- Dong goi so nguyen vao byte theo thu tu byte mang (big-endian) ---------- */

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v);
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

static void put_u64(uint8_t *p, uint64_t v) {
    put_u32(p, (uint32_t)(v >> 32));
    put_u32(p + 4, (uint32_t)(v));
}

static uint16_t get_u16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t get_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static uint64_t get_u64(const uint8_t *p) {
    return ((uint64_t)get_u32(p) << 32) | get_u32(p + 4);
}

/*
 * bridge_encode_header
 * Y nghia : Chuyen header (so nguyen cua may) thanh 24 byte theo thu tu byte mang.
 *           Tu viet thay cho htonl/htons de ro tung byte nam o dau (xem bang trong bridge.h).
 * Dau vao : h   - header can dong goi
 * Dau ra  : out - mang 24 byte nhan ket qua
 */
void bridge_encode_header(const bridge_header *h, uint8_t out[BRIDGE_HEADER_SIZE]) {
    put_u16(out + 0, BRIDGE_MAGIC);
    out[2] = BRIDGE_VERSION;
    out[3] = h->kind;
    put_u16(out + 4, h->type);
    put_u16(out + 6, h->status);
    put_u32(out + 8, h->conn_id);
    put_u64(out + 12, h->req_id);
    put_u32(out + 20, h->length);
}

/*
 * bridge_decode_header
 * Y nghia : Giai ma 24 byte nhan duoc thanh header, dong thoi kiem tra hop le.
 * Dau vao : in - 24 byte vua doc tu socket
 * Dau ra  : h  - header da giai ma (chi dung khi tra ve BRIDGE_DECODE_OK)
 *           Tra ve BRIDGE_DECODE_OK, hoac BAD_MAGIC / BAD_VERSION / TOO_LARGE.
 */
int bridge_decode_header(const uint8_t in[BRIDGE_HEADER_SIZE], bridge_header *h) {
    if (get_u16(in) != BRIDGE_MAGIC) return BRIDGE_DECODE_BAD_MAGIC;
    if (in[2] != BRIDGE_VERSION)     return BRIDGE_DECODE_BAD_VERSION;

    h->kind    = in[3];
    h->type    = get_u16(in + 4);
    h->status  = get_u16(in + 6);
    h->conn_id = get_u32(in + 8);
    h->req_id  = get_u64(in + 12);
    h->length  = get_u32(in + 20);
    if (h->length > BRIDGE_MAX_PAYLOAD) return BRIDGE_DECODE_TOO_LARGE;
    return BRIDGE_DECODE_OK;
}

/*
 * send_all
 * Y nghia : Gui het "len" byte. send() co the chi gui duoc mot phan nen phai lap.
 * Dau vao : fd, buf, len
 * Dau ra  : 0 neu da gui het; -1 neu loi (da in perror).
 */
static int send_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    while (len > 0) {
        /* MSG_NOSIGNAL: neu ben kia da dong thi nhan loi EPIPE thay vi bi he dieu hanh kill bang SIGPIPE */
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;   /* bi tin hieu ngat giua chung, thu lai */
            perror("send");
            return -1;
        }
        p   += n;
        len -= (size_t)n;
    }
    return 0;
}

/*
 * recv_all
 * Y nghia : Doc DUNG "len" byte. recv() co the tra ve it hon nen phai lap cho den khi du.
 * Dau vao : fd, buf, len
 * Dau ra  : 1  neu da doc du len byte
 *           0  neu ben kia dong ket noi NGAY TRUOC byte dau tien (dong sach se, khong mat du lieu)
 *           -1 neu loi, hoac ben kia dong khi moi doc duoc mot phan (khung bi cat ngang)
 */
static int recv_all(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recv");
            return -1;
        }
        if (n == 0) {                        /* ben kia da dong ket noi */
            if (got == 0) return 0;
            fprintf(stderr, "recv: ket noi dong khi dang doc do (%zu/%zu byte)\n", got, len);
            return -1;
        }
        got += (size_t)n;
    }
    return 1;
}

/*
 * bridge_connect
 * Y nghia : Tao socket TCP va ket noi toi server Go (Buoc 1-3 cua client TCP).
 * Dau vao : ip   - dia chi IPv4 dang chuoi (vd "127.0.0.1")
 *           port - cong cua bridge (mac dinh 9100)
 * Dau ra  : file descriptor (>= 0) neu thanh cong; -1 neu that bai (da in loi).
 */
int bridge_connect(const char *ip, int port) {
    /* Buoc 1: tao socket TCP */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        return -1;
    }

    /* Buoc 2: khai bao dia chi server */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "dia chi IP khong hop le: %s\n", ip);
        close(sock);
        return -1;
    }

    /* Buoc 3: ket noi */
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }

    /* Khung nho, can gui ngay: tat thuat toan Nagle (gom nhieu goi nho truoc khi gui) de giam do tre */
    int one = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    return sock;
}

/*
 * bridge_send_frame
 * Y nghia : Gui mot khung (header + payload) len duong noi bo.
 * Dau vao : fd      - socket da ket noi
 *           h       - header (truong length se duoc ghi de bang "len")
 *           payload - du lieu JSON, co the NULL neu len = 0
 *           len     - so byte payload
 * Dau ra  : 0 neu thanh cong; -1 neu loi hoac payload qua lon.
 */
int bridge_send_frame(int fd, const bridge_header *h, const char *payload, size_t len) {
    if (len > BRIDGE_MAX_PAYLOAD) {
        fprintf(stderr, "payload qua lon: %zu byte\n", len);
        return -1;
    }

    bridge_header copy = *h;
    copy.length = (uint32_t)len;

    uint8_t hdr[BRIDGE_HEADER_SIZE];
    bridge_encode_header(&copy, hdr);

    if (send_all(fd, hdr, sizeof(hdr)) < 0) return -1;
    if (len > 0 && send_all(fd, payload, len) < 0) return -1;
    return 0;
}

/*
 * bridge_recv_frame
 * Y nghia : Doc dung mot khung: doc du 24 byte header, kiem tra, roi doc du "length" byte payload.
 * Dau vao : fd - socket da ket noi
 * Dau ra  : f  - khung nhan duoc (nho goi bridge_frame_free sau khi dung)
 *           Tra ve 0 neu thanh cong; 1 neu server dong ket noi sach se; -1 neu loi.
 */
int bridge_recv_frame(int fd, bridge_frame *f) {
    uint8_t hdr[BRIDGE_HEADER_SIZE];
    f->payload = NULL;

    int rc = recv_all(fd, hdr, sizeof(hdr));
    if (rc == 0) return 1;
    if (rc < 0)  return -1;

    int dec = bridge_decode_header(hdr, &f->hdr);
    if (dec != BRIDGE_DECODE_OK) {
        fprintf(stderr, "header khong hop le (ma %d)\n", dec);
        return -1;
    }

    if (f->hdr.length > 0) {
        f->payload = (char *)malloc((size_t)f->hdr.length + 1);   /* +1 cho '\0' */
        if (f->payload == NULL) {
            perror("malloc");
            return -1;
        }
        if (recv_all(fd, f->payload, f->hdr.length) != 1) {
            free(f->payload);
            f->payload = NULL;
            return -1;
        }
        f->payload[f->hdr.length] = '\0';
    }
    return 0;
}

/*
 * bridge_frame_free
 * Y nghia : Giai phong payload cua khung (an toan khi payload = NULL).
 */
void bridge_frame_free(bridge_frame *f) {
    free(f->payload);
    f->payload = NULL;
}

/*
 * bridge_wait_readable
 * Y nghia : Cho toi da timeout_ms mili-giay cho den khi socket co du lieu de doc.
 *           Dung de khong bi "treo" mai o recv() khi chi muon doi tin PUSH trong mot khoang thoi gian.
 * Dau vao : fd, timeout_ms
 * Dau ra  : 1 neu co du lieu (hoac ben kia dong), 0 neu het thoi gian, -1 neu loi.
 */
int bridge_wait_readable(int fd, int timeout_ms) {
    struct pollfd pfd;
    pfd.fd      = fd;
    pfd.events  = POLLIN;
    pfd.revents = 0;

    int rc;
    do {
        rc = poll(&pfd, 1, timeout_ms);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0) {
        perror("poll");
        return -1;
    }
    return rc > 0 ? 1 : 0;
}

const char *bridge_kind_name(uint8_t kind) {
    switch (kind) {
    case BRIDGE_REQUEST:    return "REQUEST";
    case BRIDGE_RESPONSE:   return "RESPONSE";
    case BRIDGE_PUSH:       return "PUSH";
    case BRIDGE_CONN_OPEN:  return "CONN_OPEN";
    case BRIDGE_CONN_CLOSE: return "CONN_CLOSE";
    case BRIDGE_PING:       return "PING";
    case BRIDGE_PONG:       return "PONG";
    case BRIDGE_KICK:       return "KICK";
    default:                return "UNKNOWN";
    }
}

const char *bridge_type_name(uint16_t type) {
    switch (type) {
    case MSG_SYS_HEALTH:    return "SYS_HEALTH";
    case MSG_AUTH_REGISTER: return "AUTH_REGISTER";
    case MSG_AUTH_LOGIN:    return "AUTH_LOGIN";
    case MSG_AUTH_LOGOUT:   return "AUTH_LOGOUT";
    case MSG_AUTH_TOKEN:    return "AUTH_TOKEN";
    case MSG_FLIGHT_SEARCH: return "FLIGHT_SEARCH";
    case MSG_FLIGHT_GET:    return "FLIGHT_GET";
    case MSG_FLIGHT_LIST:   return "FLIGHT_LIST";
    default:                return "?";
    }
}
