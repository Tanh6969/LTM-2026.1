#include "bridge.h"
#include "echo_transport.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct { int client_fd; char bridge_ip[64]; int bridge_port; uint32_t conn_id; } client_arg;
static atomic_uint next_conn_id=1;

static void send_client_error(int fd,uint32_t seq,uint64_t req,uint16_t status,const char *message){echo_header h={.type=ECHO_TYPE_RESPONSE,.app_type=status,.seq=seq,.req_id=req,.length=(uint32_t)strlen(message)};(void)echo_send_frame(fd,&h,message);}

static void *serve_client(void *opaque){client_arg *a=opaque;int cfd=a->client_fd;uint32_t cid=a->conn_id;char bridge_ip[64];strcpy(bridge_ip,a->bridge_ip);int bridge_port=a->bridge_port;free(a);
    int bfd=bridge_connect(bridge_ip,bridge_port);if(bfd<0){send_client_error(cfd,0,0,503,"Go bridge unavailable");close(cfd);return NULL;}
    bridge_header meta={0};meta.kind=BRIDGE_CONN_OPEN;meta.conn_id=cid;const char *open_json="{\"remote\":\"tcp-client\",\"transport\":\"tcp\"}";
    if(bridge_send_frame(bfd,&meta,open_json,strlen(open_json))<0){close(bfd);close(cfd);return NULL;}
    for(;;){echo_frame request;int rc=echo_recv_frame(cfd,&request,60000);if(rc==1)break;if(rc==-2)continue;if(rc<0)break;
        if(request.header.type==ECHO_TYPE_PING){echo_header pong={.type=ECHO_TYPE_PONG,.seq=request.header.seq};(void)echo_send_frame(cfd,&pong,NULL);echo_frame_free(&request);continue;}
        if(request.header.type!=ECHO_TYPE_REQUEST){echo_frame_free(&request);break;}
        bridge_header h={0};h.kind=BRIDGE_REQUEST;h.type=request.header.app_type;h.conn_id=cid;h.req_id=request.header.req_id;h.length=request.header.length;
        if(bridge_send_frame(bfd,&h,(const char*)request.payload,request.header.length)<0){send_client_error(cfd,request.header.seq,request.header.req_id,503,"bridge write failed");echo_frame_free(&request);break;}
        bridge_frame response;rc=bridge_recv_frame(bfd,&response);if(rc!=0){send_client_error(cfd,request.header.seq,request.header.req_id,503,"bridge response unavailable");echo_frame_free(&request);break;}
        if(response.hdr.kind==BRIDGE_RESPONSE){echo_header out={.type=ECHO_TYPE_RESPONSE,.app_type=response.hdr.status,.seq=request.header.seq,.req_id=response.hdr.req_id,.length=response.hdr.length};(void)echo_send_frame(cfd,&out,response.payload);}
        bridge_frame_free(&response);echo_frame_free(&request);
    }
    bridge_header close_h={0};close_h.kind=BRIDGE_CONN_CLOSE;close_h.conn_id=cid;(void)bridge_send_frame(bfd,&close_h,NULL,0);shutdown(bfd,SHUT_RDWR);close(bfd);shutdown(cfd,SHUT_RDWR);close(cfd);return NULL;
}

int main(int argc,char **argv){int client_port=argc>1?atoi(argv[1]):9000;const char *bridge_ip=argc>2?argv[2]:"127.0.0.1";int bridge_port=argc>3?atoi(argv[3]):9100;signal(SIGPIPE,SIG_IGN);
    int listener=socket(AF_INET,SOCK_STREAM,0);if(listener<0){perror("socket");return 1;}int one=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));struct sockaddr_in addr={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_ANY),.sin_port=htons((uint16_t)client_port)};
    if(bind(listener,(struct sockaddr*)&addr,sizeof(addr))<0||listen(listener,128)<0){perror("bind/listen");close(listener);return 1;}printf("C gateway listening on %d; Go bridge %s:%d\n",client_port,bridge_ip,bridge_port);
    for(;;){int fd=accept(listener,NULL,NULL);if(fd<0){if(errno==EINTR)continue;perror("accept");continue;}client_arg *a=calloc(1,sizeof(*a));if(!a){close(fd);continue;}a->client_fd=fd;a->conn_id=atomic_fetch_add(&next_conn_id,1);strncpy(a->bridge_ip,bridge_ip,sizeof(a->bridge_ip)-1);a->bridge_port=bridge_port;pthread_t t;if(pthread_create(&t,NULL,serve_client,a)){close(fd);free(a);continue;}pthread_detach(t);}
}
