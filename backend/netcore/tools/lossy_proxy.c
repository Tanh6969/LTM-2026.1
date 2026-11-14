#include "echo_transport.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct { int client; char host[64]; int server_port; int drop_reply; int corrupt; int delay_ms; } proxy_args;
static int dial(const char*h,int p){int f=socket(AF_INET,SOCK_STREAM,0);if(f<0)return -1;struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons((uint16_t)p)};if(inet_pton(AF_INET,h,&a.sin_addr)!=1||connect(f,(struct sockaddr*)&a,sizeof(a))<0){close(f);return -1;}return f;}
static void *relay(void *v){proxy_args*a=v;int upstream=dial(a->host,a->server_port);if(upstream<0){close(a->client);free(a);return NULL;}int c=a->client;unsigned char buf[8192];int drop=a->drop_reply,corrupt=a->corrupt;for(;;){struct pollfd p[2]={{.fd=c,.events=POLLIN},{.fd=upstream,.events=POLLIN}};int n=poll(p,2,-1);if(n<=0)break;for(int i=0;i<2;++i)if(p[i].revents&POLLIN){int from=p[i].fd,to=i?c:upstream;ssize_t got=recv(from,buf,sizeof(buf),0);if(got<=0)goto done;if(a->delay_ms)usleep((useconds_t)a->delay_ms*1000u);
            if(i==1&&drop){drop=0;fprintf(stderr,"proxy: dropped one server reply chunk\n");continue;}
            if(i==1&&corrupt&&got>(ssize_t)ECHO_HEADER_SIZE){buf[ECHO_HEADER_SIZE]^=1;corrupt=0;fprintf(stderr,"proxy: corrupted one payload byte\n");}
            size_t off=0;while(off<(size_t)got){ssize_t sent=send(to,buf+off,(size_t)got-off,0);if(sent<=0)goto done;off+=(size_t)sent;}
        }if(p[0].revents&(POLLERR|POLLHUP|POLLNVAL)||p[1].revents&(POLLERR|POLLHUP|POLLNVAL))break;}
done:shutdown(c,SHUT_RDWR);shutdown(upstream,SHUT_RDWR);close(c);close(upstream);free(a);return NULL;}
int main(int argc,char**argv){if(argc<3){fprintf(stderr,"Usage: %s listen_port server_port [drop_reply_once] [corrupt_reply_once] [delay_ms]\n",argv[0]);return 2;}int lp=atoi(argv[1]),sp=atoi(argv[2]);int drop=argc>3?atoi(argv[3]):0,corrupt=argc>4?atoi(argv[4]):0,delay=argc>5?atoi(argv[5]):0;int l=socket(AF_INET,SOCK_STREAM,0),one=1;setsockopt(l,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));struct sockaddr_in a={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_ANY),.sin_port=htons((uint16_t)lp)};if(bind(l,(struct sockaddr*)&a,sizeof(a))<0||listen(l,128)<0){perror("listen");return 1;}printf("Lossy proxy :%d -> 127.0.0.1:%d\n",lp,sp);for(;;){int c=accept(l,NULL,NULL);if(c<0)continue;proxy_args*x=calloc(1,sizeof(*x));if(!x){close(c);continue;}x->client=c;strcpy(x->host,"127.0.0.1");x->server_port=sp;x->drop_reply=drop;x->corrupt=corrupt;x->delay_ms=delay;pthread_t t;if(pthread_create(&t,NULL,relay,x)){close(c);free(x);continue;}pthread_detach(t);}}
