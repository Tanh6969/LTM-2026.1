/*
 * tcp_server.c - TCP Echo Server s
 *
 * Chuc nang: lang nghe tren mot cong, nhan chuoi tu client roi gui nguoc lai
 * (echo) y nguyen. Server phuc vu TUNG client mot (tuan tu).
 *
 * Luong hoat dong cua server TCP:
 *   socket() -> bind() -> listen() -> accept() -> recv()/send() -> close()
 *
 * Bien dich: make        Chay: ./tcp_server [port]   (mac dinh 5550)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>      /* close() */
#include <sys/types.h>
#include <sys/socket.h>  /* socket, bind, listen, accept, send, recv */
#include <netinet/in.h>  /* struct sockaddr_in, htons, htonl */
#include <arpa/inet.h>   /* inet_ntoa, ntohs */

#define DEFAULT_PORT 5550  /* Cong mac dinh server lang nghe */
#define BACKLOG      5     /* So ket noi toi da cho trong hang doi cua listen() */
#define BUFF_SIZE    1024  /* Kich thuoc bo dem nhan du lieu */

/*
 * create_listen_socket
 * Y nghia : Thuc hien Buoc 1-3: tao socket, gan dia chi (bind), bat che do lang nghe.
 * Dau vao : port - cong TCP can mo (vd 5550).
 * Dau ra  : file descriptor cua socket lang nghe (>= 0) neu thanh cong;
 *           -1 neu that bai (da in loi bang perror).
 */
int create_listen_socket(int port) {
    /* Buoc 1: Tao socket. AF_INET = IPv4, SOCK_STREAM = TCP, 0 = giao thuc mac dinh */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        return -1;
    }

    /* (Tuy chon) Cho phep dung lai cong ngay sau khi tat server,
     * tranh loi "Address already in use". */
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Buoc 2: Khai bao dia chi server va gan (bind) vao socket */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));          /* xoa sach cau truc truoc khi dung */
    addr.sin_family      = AF_INET;          /* IPv4 */
    addr.sin_port        = htons(port);      /* htons: doi thu tu byte may -> mang */
    addr.sin_addr.s_addr = htonl(INADDR_ANY);/* nhan ket noi tu moi card mang */

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(sock);
        return -1;
    }

    /* Buoc 3: Chuyen socket sang trang thai lang nghe ket noi den */
    if (listen(sock, BACKLOG) == -1) {
        perror("listen");
        close(sock);
        return -1;
    }
    return sock;
}

/*
 * handle_client
 * Y nghia : Phuc vu mot client da ket noi: nhan du lieu va gui lai (echo)
 *           cho den khi client dong ket noi.
 * Dau vao : conn_sock - socket ket noi voi client (tra ve tu accept()).
 * Dau ra  : khong co (ham tra ve khi client ngat ket noi hoac co loi).
 */
void handle_client(int conn_sock) {
    char buff[BUFF_SIZE];
    ssize_t received, sent;

    while (1) {
        /* recv() chan (blocking) den khi co du lieu.
         * Tra ve: so byte nhan duoc; 0 = client dong ket noi; -1 = loi.
         * Chu y: TCP la dong byte, khong bao toan ranh gioi "tin nhan". */
        received = recv(conn_sock, buff, BUFF_SIZE - 1, 0);
        if (received <= 0) {
            printf("Client disconnected.\n");
            break;
        }
        buff[received] = '\0';           /* ket thuc chuoi de in ra bang printf */
        printf("Received: %s", buff);

        /* Gui lai dung so byte vua nhan. Tra ve so byte da gui hoac -1 neu loi */
        sent = send(conn_sock, buff, received, 0);
        if (sent <= 0) {
            printf("Cannot send data, closing.\n");
            break;
        }
    }
}

int main(int argc, char *argv[]) {
    int port = (argc > 1) ? atoi(argv[1]) : DEFAULT_PORT;

    /* Buoc 1-3: tao socket lang nghe */
    int listen_sock = create_listen_socket(port);
    if (listen_sock == -1) return 1;
    printf("Server listening on port %d ...\n", port);

    /* Buoc 4: vong lap chap nhan tung client */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        /* accept() chan den khi co client ket noi. Tra ve socket MOI
         * danh rieng cho client do (listen_sock van tiep tuc lang nghe). */
        int conn_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (conn_sock == -1) {
            perror("accept");
            continue;
        }
        printf("Connection from %s:%d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        /* Buoc 5: trao doi du lieu voi client */
        handle_client(conn_sock);

        /* Buoc 6: dong socket cua client nay */
        close(conn_sock);
    }

    close(listen_sock);  /* (khong toi duoc day; dung Ctrl+C de tat server) */
    return 0;
}
