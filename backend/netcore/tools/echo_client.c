#include "echo_transport.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int connect_to(const char *ip,int port){int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return -1;struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons((uint16_t)port)};if(inet_pton(AF_INET,ip,&a.sin_addr)!=1||connect(fd,(struct sockaddr*)&a,sizeof(a))<0){close(fd);return -1;}return fd;}
static uint64_t make_id(void){struct timespec t;clock_gettime(CLOCK_REALTIME,&t);return ((uint64_t)t.tv_sec<<32)^(uint64_t)t.tv_nsec^(uint64_t)getpid();}

int main(int argc,char **argv){const char*ip=argc>1?argv[1]:"127.0.0.1";int port=argc>2?atoi(argv[2]):9000;uint16_t app_type=argc>3?(uint16_t)strtoul(argv[3],NULL,0):1;int fd=connect_to(ip,port);if(fd<0){perror("connect");return 1;}printf("Connected to %s:%d; app type=%u; enter JSON (\"/quit\" to exit)\n",ip,port,app_type);
    char *line=NULL;size_t cap=0;uint32_t seq=1;while(getline(&line,&cap,stdin)>0){size_t len=strlen(line);if(len&&line[len-1]=='\n')line[--len]='\0';if(strcmp(line,"/quit")==0)break;if(len>ECHO_MAX_PAYLOAD){fprintf(stderr,"message too long\n");continue;}
        uint64_t req=make_id();int done=0;for(unsigned attempt=0;attempt<4&&!done;++attempt){echo_header h={.type=ECHO_TYPE_REQUEST,.app_type=app_type,.flags=attempt?ECHO_FLAG_RETRANS:0,.seq=seq,.req_id=req,.length=(uint32_t)len};if(echo_send_frame(fd,&h,line)<0){perror("send");goto out;}for(;;){echo_frame r;int rc=echo_recv_frame(fd,&r,2000);if(rc==0){if(r.header.type==ECHO_TYPE_PING){echo_header pong={.type=ECHO_TYPE_PONG,.seq=r.header.seq,.req_id=0,.length=r.header.length};echo_send_frame(fd,&pong,r.payload);echo_frame_free(&r);continue;}if(r.header.type==ECHO_TYPE_RESPONSE&&r.header.req_id==req){printf("Response (status=%u): ",r.header.app_type);if(r.header.length)fwrite(r.payload,1,r.header.length,stdout);putchar('\n');done=1;}echo_frame_free(&r);break;}if(rc!=-2){fprintf(stderr,"connection/protocol error\n");goto out;}fprintf(stderr,"timeout; retry %u/3\n",attempt+1);break;}}
        if(!done)fprintf(stderr,"no response after retries\n");++seq;
    }
out:free(line);close(fd);return 0;}
