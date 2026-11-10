/*
 * test_bridge.c - Kiem tra dong goi/giai ma header cua bridge
 *
 * Quan trong: cac byte "mong doi" ben duoi la BAN SAO tung byte voi test ben Go
 * (internal/infra/bridge/protocol_test.go, TestEncodeDecodeRoundTrip). Hai ben cung dung
 * mot chuoi byte nghia la C va Go hieu header giong nhau.
 *
 * Bien dich va chay: make test
 */

#include <stdio.h>
#include <string.h>

#include "bridge.h"

static int failures = 0;

#define CHECK(cond, msg)                                   \
    do {                                                   \
        if (!(cond)) {                                     \
            printf("FAIL: %s (dong %d)\n", msg, __LINE__); \
            failures++;                                    \
        }                                                  \
    } while (0)

/* header giong test ben Go: kind=REQUEST, type=AUTH_LOGIN, conn=7, req=0x0102030405060708, length=33 */
static void test_encode_matches_go(void) {
    bridge_header h = {BRIDGE_REQUEST, MSG_AUTH_LOGIN, 0, 7, 0x0102030405060708ULL, 33};
    uint8_t out[BRIDGE_HEADER_SIZE];
    bridge_encode_header(&h, out);

    const uint8_t want[BRIDGE_HEADER_SIZE] = {
        0x4C, 0x54, 1, BRIDGE_REQUEST, 0x01, 0x02, 0, 0,
        0, 0, 0, 7,
        1, 2, 3, 4, 5, 6, 7, 8,
        0, 0, 0, 33};
    CHECK(memcmp(out, want, sizeof(want)) == 0, "header C phai trung tung byte voi header Go");
}

static void test_round_trip(void) {
    bridge_header in = {BRIDGE_RESPONSE, MSG_FLIGHT_SEARCH, 404, 0xFFFFFFFFu, 0xFFFFFFFFFFFFFFFFULL, 1024};
    uint8_t buf[BRIDGE_HEADER_SIZE];
    bridge_header out;

    bridge_encode_header(&in, buf);
    CHECK(bridge_decode_header(buf, &out) == BRIDGE_DECODE_OK, "giai ma header hop le");
    CHECK(out.kind == in.kind && out.type == in.type && out.status == in.status, "kind/type/status");
    CHECK(out.conn_id == in.conn_id && out.req_id == in.req_id && out.length == in.length, "conn/req/length");
}

static void test_decode_rejects_bad_input(void) {
    bridge_header h = {BRIDGE_REQUEST, MSG_SYS_HEALTH, 0, 1, 1, 0};
    uint8_t buf[BRIDGE_HEADER_SIZE];
    bridge_header out;

    bridge_encode_header(&h, buf);
    buf[0] = 0;
    CHECK(bridge_decode_header(buf, &out) == BRIDGE_DECODE_BAD_MAGIC, "sai magic bi tu choi");

    bridge_encode_header(&h, buf);
    buf[2] = 99;
    CHECK(bridge_decode_header(buf, &out) == BRIDGE_DECODE_BAD_VERSION, "sai version bi tu choi");

    h.length = BRIDGE_MAX_PAYLOAD + 1;
    bridge_encode_header(&h, buf);
    CHECK(bridge_decode_header(buf, &out) == BRIDGE_DECODE_TOO_LARGE, "payload qua lon bi tu choi");
}

int main(void) {
    test_encode_matches_go();
    test_round_trip();
    test_decode_rejects_bad_input();

    if (failures == 0) printf("OK: tat ca test cua bridge deu dat\n");
    return failures == 0 ? 0 : 1;
}
