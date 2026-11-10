/*
 * bridge_cli.c - Client console de thu duong noi bo netcore <-> Go
 *
 * Chuc nang: ket noi toi server Go (cong bridge), gui yeu cau nghiep vu bang khung
 * theo docs/BRIDGE-PROTOCOL.md va in phan hoi. Dong vai "netcore" cho mot client duy nhat
 * (conn_id = 1), dung de kiem tra Go + usecase + DB hoat dong truoc khi netcore day du hoan thanh.
 *
 * Luong hoat dong:
 *   connect() -> gui CONN_OPEN -> lap: doc lenh -> gui REQUEST -> doi RESPONSE -> in ra
 *
 * Bien dich: make            (tu thu muc netcore)
 * Chay     : ./bin/bridge_cli [-q] [ip] [port]   (mac dinh 127.0.0.1 9100)
 *            -q : khong in loi nhac/nhac lenh, tien cho chay bang kich ban (pipe)
 * Lenh     : go "help" khi dang chay
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>   /* send() */

#include "bridge.h"

#define DEFAULT_ADDR "127.0.0.1"
#define DEFAULT_PORT 9100
#define LINE_SIZE    4096
#define MAX_ARGS     16
#define JSON_SIZE    8192   /* du cho 4 truong x 1024 byte da escape + khung JSON */

static int      g_fd       = -1;
static uint32_t g_conn_id  = 1;     /* mot client duy nhat nen chon conn_id co dinh */
static uint64_t g_next_req = 1;     /* so yeu cau tang dan, de ghep REQUEST voi RESPONSE */
static int      g_quiet    = 0;

/*
 * now_ms
 * Y nghia : Lay thoi gian hien tai theo mili-giay (dong ho don dieu, khong bi lui khi chinh gio).
 */
static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/*
 * json_escape
 * Y nghia : Chuyen chuoi thanh dang an toan de nhung vao JSON (them \ truoc dau " va \, ma hoa ky tu dieu khien).
 *           Neu khong lam buoc nay, mat khau co dau " se lam hong JSON.
 * Dau vao : in - chuoi goc; cap - kich thuoc bo dem out
 * Dau ra  : out - chuoi da escape, luon ket thuc bang '\0'
 */
static void json_escape(const char *in, char *out, size_t cap) {
    size_t o = 0;
    for (; *in && o + 7 < cap; in++) {
        unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c < 0x20) {
            o += (size_t)snprintf(out + o, cap - o, "\\u%04x", c);
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

/*
 * send_request
 * Y nghia : Gui mot REQUEST len Go, tra ve req_id de doi phan hoi tuong ung.
 * Dau vao : type - loai thong diep; json - payload (NULL = rong)
 * Dau ra  : req_id (>= 1), hoac 0 neu gui loi.
 */
static uint64_t send_request(uint16_t type, const char *json) {
    bridge_header h;
    memset(&h, 0, sizeof(h));
    h.kind    = BRIDGE_REQUEST;
    h.type    = type;
    h.conn_id = g_conn_id;
    h.req_id  = g_next_req++;

    size_t len = json ? strlen(json) : 0;
    if (bridge_send_frame(g_fd, &h, json, len) < 0) return 0;
    return h.req_id;
}

/*
 * print_frame
 * Y nghia : In mot khung nhan duoc theo dinh dang de doc (dong dau la tom tat, dong sau la payload).
 */
static void print_frame(const bridge_frame *f, long elapsed_ms) {
    const bridge_header *h = &f->hdr;
    printf("<< %s %s", bridge_kind_name(h->kind), bridge_type_name(h->type));
    if (h->kind == BRIDGE_RESPONSE) printf(" status=%u", h->status);
    if (h->req_id) printf(" req=%llu", (unsigned long long)h->req_id);
    if (h->kind == BRIDGE_PUSH || h->kind == BRIDGE_KICK) printf(" conn=%u", h->conn_id);
    if (elapsed_ms >= 0) printf(" (%ldms)", elapsed_ms);
    printf("\n");
    if (f->payload) printf("%s\n", f->payload);
    fflush(stdout);
}

/*
 * await_response
 * Y nghia : Doc khung cho den khi gap RESPONSE co req_id dung nhu da gui.
 *           Cac tin PUSH den giua chung duoc in ra luon (khong lam mat tin day).
 * Dau vao : req_id - so yeu cau dang doi; started - thoi diem bat dau (ms) de do do tre
 * Dau ra  : status cua RESPONSE (>= 0); -1 neu mat ket noi hoac loi.
 */
static int await_response(uint64_t req_id, long started) {
    for (;;) {
        bridge_frame f;
        int rc = bridge_recv_frame(g_fd, &f);
        if (rc == 1) {
            printf("!! server da dong ket noi\n");
            return -1;
        }
        if (rc < 0) return -1;

        if (f.hdr.kind == BRIDGE_RESPONSE && f.hdr.req_id == req_id) {
            int status = f.hdr.status;
            print_frame(&f, now_ms() - started);
            bridge_frame_free(&f);
            return status;
        }
        print_frame(&f, -1);          /* PUSH hoac khung khong ghep duoc */
        bridge_frame_free(&f);
    }
}

/*
 * do_request
 * Y nghia : Gui REQUEST roi doi RESPONSE. Ham dung chung cho moi lenh nghiep vu.
 * Dau ra  : status cua phan hoi, hoac -1 neu loi ket noi.
 */
static int do_request(uint16_t type, const char *json) {
    long started = now_ms();
    uint64_t id = send_request(type, json);
    if (id == 0) return -1;
    return await_response(id, started);
}

/*
 * do_slow_request
 * Y nghia : Gui REQUEST nhung chia nho thanh TUNG BYTE, cach nhau vai mili-giay.
 *           Dung de chung minh ben nhan ghep dung khung du TCP chia nho du lieu (bai hoc "framing").
 * Dau vao : type, json
 * Dau ra  : status cua phan hoi, hoac -1 neu loi.
 */
static int do_slow_request(uint16_t type, const char *json) {
    bridge_header h;
    memset(&h, 0, sizeof(h));
    h.kind    = BRIDGE_REQUEST;
    h.type    = type;
    h.conn_id = g_conn_id;
    h.req_id  = g_next_req++;
    h.length  = (uint32_t)strlen(json);

    uint8_t hdr[BRIDGE_HEADER_SIZE];
    bridge_encode_header(&h, hdr);

    long started = now_ms();
    for (size_t i = 0; i < sizeof(hdr); i++) {
        if (send(g_fd, hdr + i, 1, 0) != 1) return -1;
        usleep(3000);
    }
    for (size_t i = 0; i < h.length; i++) {
        if (send(g_fd, json + i, 1, 0) != 1) return -1;
        usleep(3000);
    }
    return await_response(h.req_id, started);
}

/*
 * wait_pushes
 * Y nghia : Lang nghe tin PUSH tu server trong "seconds" giay (vd thong bao tre chuyen bay).
 */
static void wait_pushes(int seconds) {
    long deadline = now_ms() + (long)seconds * 1000;
    for (;;) {
        long left = deadline - now_ms();
        if (left <= 0) break;
        int rc = bridge_wait_readable(g_fd, (int)left);
        if (rc <= 0) break;

        bridge_frame f;
        rc = bridge_recv_frame(g_fd, &f);
        if (rc != 0) {
            printf("!! server da dong ket noi\n");
            return;
        }
        print_frame(&f, -1);
        bridge_frame_free(&f);
    }
    printf("-- het thoi gian cho\n");
}

/*
 * tokenize
 * Y nghia : Tach dong lenh thanh cac tu theo khoang trang; cum trong dau " " duoc coi la mot tu
 *           (de nhap ten thanh pho co dau cach, vd "Ho Chi Minh"). Sua truc tiep tren line.
 * Dau ra  : so tu tach duoc (toi da max)
 */
static int tokenize(char *line, char *argv[], int max) {
    int n = 0;
    char *p = line;
    while (*p && n < max) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (*p == '"') {
            argv[n++] = ++p;
            while (*p && *p != '"') p++;
        } else {
            argv[n++] = p;
            while (*p && !isspace((unsigned char)*p)) p++;
        }
        if (*p) *p++ = '\0';
    }
    return n;
}

static void print_help(void) {
    printf(
        "Lenh:\n"
        "  health                           kiem tra server Go\n"
        "  register HO TEN EMAIL MATKHAU    dang ky tai khoan khach hang\n"
        "  login EMAIL MATKHAU              dang nhap (ket noi nay duoc gan danh tinh)\n"
        "  token TOKEN                      gan danh tinh bang token da co\n"
        "  logout                           dang xuat\n"
        "  search DI DEN YYYY-MM-DD         tim chuyen bay (ten co dau cach thi dat trong \"...\")\n"
        "  flight ID                        chi tiet chuyen bay\n"
        "  list [TRANG] [SO_LUONG]          danh sach chuyen bay\n"
        "  raw HEXTYPE JSON                 gui thong diep tuy y, vd: raw 0x0201 {\"departureCity\":\"...\"}\n"
        "  slow HEXTYPE JSON                nhu raw nhung gui tung byte co tre (thu ghep khung)\n"
        "  ping                             kiem tra duong noi con song\n"
        "  wait GIAY                        doi tin PUSH tu server\n"
        "  badframe                         gui rac de xem server dong ket noi\n"
        "  quit\n");
}

/*
 * run_command
 * Y nghia : Thuc thi mot dong lenh nguoi dung nhap.
 * Dau ra  : 0 de tiep tuc, 1 de thoat chuong trinh.
 */
static int run_command(char *line) {
    char json[JSON_SIZE];
    char a[LINE_SIZE / 4], b[LINE_SIZE / 4], c[LINE_SIZE / 4], d[LINE_SIZE / 4];

    /* "raw" va "slow" lay nguyen phan con lai cua dong lam JSON nen xu ly truoc khi tach tu */
    if (strncmp(line, "raw ", 4) == 0 || strncmp(line, "slow ", 5) == 0) {
        int slow = line[0] == 's';
        char *rest = line + (slow ? 5 : 4);
        char *end;
        unsigned long type = strtoul(rest, &end, 0);
        while (*end && isspace((unsigned char)*end)) end++;
        if (end == rest || *end == '\0') {
            printf("dung: %s HEXTYPE JSON\n", slow ? "slow" : "raw");
            return 0;
        }
        if (slow) do_slow_request((uint16_t)type, end);
        else      do_request((uint16_t)type, end);
        return 0;
    }

    char *argv[MAX_ARGS];
    int argc = tokenize(line, argv, MAX_ARGS);
    if (argc == 0) return 0;
    const char *cmd = argv[0];

    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) return 1;

    if (strcmp(cmd, "help") == 0) {
        print_help();
    } else if (strcmp(cmd, "health") == 0) {
        do_request(MSG_SYS_HEALTH, "{}");
    } else if (strcmp(cmd, "register") == 0 && argc == 5) {
        json_escape(argv[1], a, sizeof(a));
        json_escape(argv[2], b, sizeof(b));
        json_escape(argv[3], c, sizeof(c));
        json_escape(argv[4], d, sizeof(d));
        snprintf(json, sizeof(json),
                 "{\"first_name\":\"%s\",\"last_name\":\"%s\",\"email\":\"%s\",\"password\":\"%s\"}", a, b, c, d);
        do_request(MSG_AUTH_REGISTER, json);
    } else if (strcmp(cmd, "login") == 0 && argc == 3) {
        json_escape(argv[1], a, sizeof(a));
        json_escape(argv[2], b, sizeof(b));
        snprintf(json, sizeof(json), "{\"email\":\"%s\",\"password\":\"%s\"}", a, b);
        do_request(MSG_AUTH_LOGIN, json);
    } else if (strcmp(cmd, "token") == 0 && argc == 2) {
        json_escape(argv[1], a, sizeof(a));
        snprintf(json, sizeof(json), "{\"token\":\"%s\"}", a);
        do_request(MSG_AUTH_TOKEN, json);
    } else if (strcmp(cmd, "logout") == 0) {
        do_request(MSG_AUTH_LOGOUT, "{}");
    } else if (strcmp(cmd, "search") == 0 && argc == 4) {
        json_escape(argv[1], a, sizeof(a));
        json_escape(argv[2], b, sizeof(b));
        json_escape(argv[3], c, sizeof(c));
        snprintf(json, sizeof(json),
                 "{\"departureCity\":\"%s\",\"arrivalCity\":\"%s\",\"flightDate\":\"%s\"}", a, b, c);
        do_request(MSG_FLIGHT_SEARCH, json);
    } else if (strcmp(cmd, "flight") == 0 && argc == 2) {
        snprintf(json, sizeof(json), "{\"flightId\":%ld}", strtol(argv[1], NULL, 10));
        do_request(MSG_FLIGHT_GET, json);
    } else if (strcmp(cmd, "list") == 0) {
        long page  = argc > 1 ? strtol(argv[1], NULL, 10) : 1;
        long limit = argc > 2 ? strtol(argv[2], NULL, 10) : 10;
        snprintf(json, sizeof(json), "{\"page\":%ld,\"limit\":%ld}", page, limit);
        do_request(MSG_FLIGHT_LIST, json);
    } else if (strcmp(cmd, "ping") == 0) {
        bridge_header h;
        memset(&h, 0, sizeof(h));
        h.kind   = BRIDGE_PING;
        h.req_id = g_next_req++;
        long started = now_ms();
        bridge_send_frame(g_fd, &h, "hi", 2);
        bridge_frame f;
        if (bridge_recv_frame(g_fd, &f) == 0) {
            print_frame(&f, now_ms() - started);
            bridge_frame_free(&f);
        }
    } else if (strcmp(cmd, "wait") == 0 && argc == 2) {
        wait_pushes((int)strtol(argv[1], NULL, 10));
    } else if (strcmp(cmd, "badframe") == 0) {
        char junk[BRIDGE_HEADER_SIZE];
        memset(junk, 'X', sizeof(junk));   /* khong co magic 0x4C54 nen server tu choi */
        send(g_fd, junk, sizeof(junk), 0);
        printf(">> da gui %d byte rac; server se dong ket noi\n", BRIDGE_HEADER_SIZE);
    } else {
        printf("lenh khong hop le hoac thieu tham so. Go \"help\".\n");
    }
    return 0;
}

int main(int argc, char *argv[]) {
    const char *ip = DEFAULT_ADDR;
    int port       = DEFAULT_PORT;
    int positional = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0) g_quiet = 1;
        else if (positional == 0) { ip = argv[i]; positional++; }
        else                      { port = atoi(argv[i]); positional++; }
    }

    /* Buoc 1-3: ket noi toi server Go */
    g_fd = bridge_connect(ip, port);
    if (g_fd == -1) return 1;
    if (!g_quiet) printf("Da ket noi toi %s:%d (conn_id=%u). Go \"help\" de xem lenh.\n", ip, port, g_conn_id);

    /* Bao cho Go biet co mot client moi */
    bridge_header open_h;
    memset(&open_h, 0, sizeof(open_h));
    open_h.kind    = BRIDGE_CONN_OPEN;
    open_h.conn_id = g_conn_id;
    const char *open_json = "{\"remote\":\"127.0.0.1:0\",\"transport\":\"cli\"}";
    if (bridge_send_frame(g_fd, &open_h, open_json, strlen(open_json)) < 0) return 1;

    /* Buoc 4: vong lap nhap lenh */
    char line[LINE_SIZE];
    for (;;) {
        if (!g_quiet) {
            printf("> ");
            fflush(stdout);
        }
        if (fgets(line, sizeof(line), stdin) == NULL) break;       /* EOF (Ctrl+D hoac het kich ban) */
        line[strcspn(line, "\r\n")] = '\0';
        if (run_command(line)) break;
    }

    /* Buoc 5: bao dong ket noi roi dong socket */
    bridge_header close_h;
    memset(&close_h, 0, sizeof(close_h));
    close_h.kind    = BRIDGE_CONN_CLOSE;
    close_h.conn_id = g_conn_id;
    bridge_send_frame(g_fd, &close_h, NULL, 0);
    close(g_fd);
    return 0;
}
