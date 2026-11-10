/*
 * tcp_client.c - TCP Echo Client (demo cho sinh vien)
 *
 * Chuc nang: ket noi toi server, doc mot dong tu ban phim, gui di,
 * nhan lai ban echo va in ra man hinh. Nhap dong trong (chi nhan Enter)
 * hoac Ctrl+D de thoat.
 *
 * Luong hoat dong cua client TCP:
 *   socket() -> connect() -> send()/recv() -> close()
 *
 * Bien dich: make
 * Chay     : ./tcp_client [server_ip] [port]   (mac dinh 127.0.0.1 5550)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>      /* close() */
#include <sys/types.h>
#include <sys/socket.h>  /* socket, connect, send, recv */
#include <netinet/in.h>  /* struct sockaddr_in, htons */
#include <arpa/inet.h>   /* inet_addr */

#define DEFAULT_ADDR "127.0.0.1"  /* Dia chi server mac dinh (may local) */
#define DEFAULT_PORT 5550         /* Cong server mac dinh */
#define BUFF_SIZE    1024         /* Kich thuoc bo dem gui/nhan */

/*
 * connect_to_server
 * Y nghia : Thuc hien Buoc 1-3: tao socket, khai bao dia chi server, ket noi.
 * Dau vao : ip   - dia chi IPv4 dang chuoi (vd "127.0.0.1")
 *           port - cong cua server
 * Dau ra  : file descriptor cua socket da ket noi (>= 0) neu thanh cong;
 *           -1 neu that bai (da in loi).
 */
int connect_to_server(const char *ip, int port) {
    /* Buoc 1: Tao socket TCP (AF_INET = IPv4, SOCK_STREAM = TCP) */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        return -1;
    }

    /* Buoc 2: Khai bao dia chi server can ket noi */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));   /* xoa sach cau truc */
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(port);      /* doi sang thu tu byte mang */
    server_addr.sin_addr.s_addr = inet_addr(ip);    /* chuoi IP -> so 32 bit */

    /* Buoc 3: Yeu cau ket noi (3-way handshake duoc thuc hien o day) */
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }
    return sock;
}

/*
 * chat_loop
 * Y nghia : Buoc 4: lap lai "nhap dong -> gui -> nhan echo -> in ra".
 * Dau vao : sock - socket da ket noi toi server.
 * Dau ra  : khong co (tra ve khi nguoi dung thoat hoac mat ket noi).
 */
void chat_loop(int sock) {
    char buff[BUFF_SIZE];

    while (1) {
        printf("Nhap chuoi can gui (Enter trong de thoat): ");

        /* fgets doc ca ky tu '\n'. Tra ve NULL khi gap EOF (Ctrl+D) */
        if (fgets(buff, BUFF_SIZE, stdin) == NULL) break;
        if (buff[0] == '\n') break;          /* dong trong -> thoat */

        /* Gui di dung do dai chuoi (khong gui ky tu '\0').
         * send() tra ve so byte da gui, hoac -1 neu loi. */
        ssize_t sent = send(sock, buff, strlen(buff), 0);
        if (sent <= 0) {
            printf("Connection closed!\n");
            break;
        }

        /* Cho nhan ban echo. recv() tra ve so byte nhan duoc,
         * 0 neu server dong ket noi, -1 neu loi. */
        ssize_t received = recv(sock, buff, BUFF_SIZE - 1, 0);
        if (received <= 0) {
            printf("Cannot receive data from server!\n");
            break;
        }
        buff[received] = '\0';               /* them '\0' de in nhu mot chuoi */
        printf("Reply from server: %s", buff);
    }
}

int main(int argc, char *argv[]) {
    const char *ip = (argc > 1) ? argv[1] : DEFAULT_ADDR;
    int port       = (argc > 2) ? atoi(argv[2]) : DEFAULT_PORT;

    /* Buoc 1-3: tao socket va ket noi */
    int sock = connect_to_server(ip, port);
    if (sock == -1) return 1;
    printf("Connected to %s:%d\n", ip, port);

    /* Buoc 4: trao doi du lieu */
    chat_loop(sock);

    /* Buoc 5: dong socket */
    close(sock);
    return 0;
}
