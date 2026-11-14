#include "echo_transport.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define DEDUPE_SLOTS 256
#define DEDUPE_TTL 300

typedef struct { uint64_t req_id; uint32_t length; uint8_t *payload; time_t saved_at; int valid; } cache_entry;
static cache_entry cache[DEDUPE_SLOTS];
static pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static size_t cache_cursor;

static int send_echo(int fd, uint16_t type, uint8_t flags, uint32_t seq, uint64_t req, const void *p, uint32_t len) {
    echo_header h = {.type=type, .flags=flags, .seq=seq, .req_id=req, .length=len};
    return echo_send_frame(fd, &h, p);
}

static void *handle_client(void *arg) {
    int fd = *(int *)arg; free(arg);
    struct timeval tv = {.tv_sec=5, .tv_usec=0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    for (;;) {
        echo_frame f;
        int rc = echo_recv_frame(fd, &f, 5000);
        if (rc == 1) break;
        if (rc == -2) { (void)send_echo(fd, ECHO_TYPE_PING, 0, 0, 0, NULL, 0); continue; }
        if (rc < 0) break;
        if (f.header.type == ECHO_TYPE_PING) {
            send_echo(fd, ECHO_TYPE_PONG, 0, f.header.seq, 0, f.payload, f.header.length);
            echo_frame_free(&f); continue;
        }
        if (f.header.type == ECHO_TYPE_PONG) { echo_frame_free(&f); continue; }
        if (f.header.type != ECHO_TYPE_REQUEST) { echo_frame_free(&f); break; }
        int replayed = 0;
        pthread_mutex_lock(&cache_mutex);
        time_t now = time(NULL);
        for (size_t i=0; i<DEDUPE_SLOTS; ++i) {
            cache_entry *e=&cache[i];
            if (e->valid && now-e->saved_at > DEDUPE_TTL) { free(e->payload); memset(e,0,sizeof(*e)); }
            if (e->valid && e->req_id==f.header.req_id) {
                send_echo(fd,ECHO_TYPE_RESPONSE,0,f.header.seq,e->req_id,e->payload,e->length); replayed=1; break;
            }
        }
        if (!replayed) {
            cache_entry *e=&cache[cache_cursor++ % DEDUPE_SLOTS];
            free(e->payload); memset(e,0,sizeof(*e));
            e->payload = f.header.length ? malloc(f.header.length) : NULL;
            if (!f.header.length || e->payload) {
                if (f.header.length) memcpy(e->payload,f.payload,f.header.length);
                e->req_id=f.header.req_id; e->length=f.header.length; e->saved_at=now; e->valid=1;
            }
            send_echo(fd,ECHO_TYPE_RESPONSE,0,f.header.seq,f.header.req_id,f.payload,f.header.length);
        }
        pthread_mutex_unlock(&cache_mutex);
        echo_frame_free(&f);
    }
    shutdown(fd, SHUT_RDWR); close(fd); return NULL;
}

int main(int argc, char **argv) {
    int port = argc > 1 ? atoi(argv[1]) : 9000;
    signal(SIGPIPE, SIG_IGN);
    int listener=socket(AF_INET,SOCK_STREAM,0); if(listener<0){perror("socket");return 1;}
    int one=1; setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
    struct sockaddr_in addr={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_ANY),.sin_port=htons((uint16_t)port)};
    if(bind(listener,(struct sockaddr*)&addr,sizeof(addr))<0||listen(listener,128)<0){perror("bind/listen");close(listener);return 1;}
    printf("Echo transport listening on %d (pthread per client)\n",port);
    for(;;){int fd=accept(listener,NULL,NULL);if(fd<0){if(errno==EINTR)continue;perror("accept");continue;}
        int *arg=malloc(sizeof(*arg));if(!arg){close(fd);continue;}*arg=fd;
        pthread_t tid;if(pthread_create(&tid,NULL,handle_client,arg)!=0){close(fd);free(arg);continue;}pthread_detach(tid);
    }
}
