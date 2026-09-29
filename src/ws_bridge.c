#define _GNU_SOURCE
#include "ws_bridge.h"
#include <arpa/inet.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct {
 int local,net; bool tls;SSL_CTX *ctx;SSL *ssl;
 char host[64];uint16_t port;char *token,*ca;
 uint8_t *rx;size_t rx_len,rx_cap;
} bridge;
static void cleanup(bridge *b){
 if(b->ssl)SSL_free(b->ssl);if(b->ctx)SSL_CTX_free(b->ctx);
 if(b->net>=0)close(b->net);close(b->local);
 if(b->token){OPENSSL_cleanse(b->token,strlen(b->token));free(b->token);}
 free(b->ca);free(b->rx);free(b);
}
static int write_all_fd(int fd,const uint8_t *data,size_t len){
 while(len){ssize_t n=send(fd,data,len,MSG_NOSIGNAL);if(n>0){data+=n;len-=(size_t)n;}
  else if(n<0&&errno==EINTR)continue;else return errno?errno:EPIPE;}
 return 0;
}
static int net_write(bridge *b,const uint8_t *data,size_t len){
 while(len){int n=b->ssl?SSL_write(b->ssl,data,(int)(len>INT_MAX?INT_MAX:len)):
   (int)send(b->net,data,len,MSG_NOSIGNAL);
  if(n>0){data+=n;len-=(size_t)n;continue;}
  if(!b->ssl&&errno==EINTR)continue;return EIO;}
 return 0;
}
static int net_read(bridge *b,uint8_t *data,size_t cap){
 int n=b->ssl?SSL_read(b->ssl,data,(int)cap):(int)recv(b->net,data,cap,0);
 if(n>0)return n;if(!b->ssl&&errno==EINTR)return -EINTR;return -ECONNRESET;
}
static int open_network(bridge *b){
 struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(b->port)};
 if(inet_pton(AF_INET,b->host,&addr.sin_addr)!=1)return EINVAL;
 int fd=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0)return errno;
 b->net=fd;int flags=fcntl(fd,F_GETFL,0);if(flags<0)return errno;
 if(fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0)return errno;
 if(connect(fd,(struct sockaddr *)&addr,sizeof(addr))<0){
  if(errno!=EINPROGRESS)return errno;
  struct pollfd p={.fd=fd,.events=POLLOUT};if(poll(&p,1,5000)<=0)return ETIMEDOUT;
  int err=0;socklen_t len=sizeof(err);if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&err,&len)<0)return errno;
  if(err)return err;
 }
 if(fcntl(fd,F_SETFL,flags)<0)return errno;
 struct timeval timeout={.tv_sec=5};(void)setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
 (void)setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
 if(!b->tls)return 0;
 b->ctx=SSL_CTX_new(TLS_client_method());if(!b->ctx)return EIO;
 SSL_CTX_set_min_proto_version(b->ctx,TLS1_2_VERSION);
 SSL_CTX_set_verify(b->ctx,SSL_VERIFY_PEER,NULL);
 if(b->ca){if(SSL_CTX_load_verify_locations(b->ctx,b->ca,NULL)!=1)return EACCES;}
 else if(SSL_CTX_set_default_verify_paths(b->ctx)!=1)return EACCES;
 b->ssl=SSL_new(b->ctx);if(!b->ssl||SSL_set_fd(b->ssl,fd)!=1)return EIO;
 if(X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(b->ssl),b->host)!=1)return EINVAL;
 if(SSL_connect(b->ssl)!=1||SSL_get_verify_result(b->ssl)!=X509_V_OK)return EACCES;
 return 0;
}
static int append_rx(bridge *b,const uint8_t *data,size_t len){
 if(len>8u*1024u*1024u+32-b->rx_len)return EMSGSIZE;
 if(b->rx_len+len>b->rx_cap){size_t cap=b->rx_cap?b->rx_cap:8192;
  while(cap<b->rx_len+len)cap*=2;uint8_t *next=realloc(b->rx,cap);if(!next)return ENOMEM;b->rx=next;b->rx_cap=cap;}
 memcpy(b->rx+b->rx_len,data,len);b->rx_len+=len;return 0;
}
static int upgrade(bridge *b){
 unsigned char nonce[16],key[25],digest[SHA_DIGEST_LENGTH],accept[29];
 if(RAND_bytes(nonce,sizeof(nonce))!=1)return EIO;
 EVP_EncodeBlock(key,nonce,sizeof(nonce));
 char joined[61];memcpy(joined,key,24);memcpy(joined+24,"258EAFA5-E914-47DA-95CA-C5AB0DC85B11",36);
 SHA1((unsigned char *)joined,60,digest);EVP_EncodeBlock(accept,digest,SHA_DIGEST_LENGTH);
 char request[1024];int n=snprintf(request,sizeof(request),
  "GET / HTTP/1.1\r\nHost: %s:%u\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: %s\r\n%s%s%s\r\n",
  b->host,b->port,key,b->token?"Authorization: Bearer ":"",b->token?b->token:"",b->token?"\r\n":"");
 if(n<0||(size_t)n>=sizeof(request))return EMSGSIZE;
 int err=net_write(b,(uint8_t *)request,(size_t)n);if(err)return err;
 for(;;){uint8_t *end=b->rx?memmem(b->rx,b->rx_len,"\r\n\r\n",4):NULL;
  if(end){size_t used=(size_t)(end-b->rx)+4;
   if(b->rx_len<12||memcmp(b->rx,"HTTP/1.1 101",12))return EPROTO;
   char *copy=strndup((char *)b->rx,used);if(!copy)return ENOMEM;
   char *field=strcasestr(copy,"Sec-WebSocket-Accept: ");
   bool good=field&&strlen(field+22)>=28&&memcmp(field+22,accept,28)==0;free(copy);if(!good)return EPROTO;
   memmove(b->rx,b->rx+used,b->rx_len-used);b->rx_len-=used;return 0;
  }
  if(b->rx_len>=8192)return EMSGSIZE;uint8_t data[4096];int got=net_read(b,data,sizeof(data));
  if(got<=0)return got==-EINTR?EINTR:ECONNRESET;
  err=append_rx(b,data,(size_t)got);if(err)return err;
 }
}
static int send_frame(bridge *b,uint8_t opcode,const uint8_t *payload,size_t len){
 size_t hdr=len<126?6:len<=65535?8:14;if(len>SIZE_MAX-hdr)return EOVERFLOW;
 uint8_t *frame=malloc(hdr+len);if(!frame)return ENOMEM;
 frame[0]=0x80|opcode;frame[1]=0x80|(len<126?(uint8_t)len:len<=65535?126:127);
 if(hdr==8){frame[2]=(uint8_t)(len>>8);frame[3]=(uint8_t)len;}
 else if(hdr==14)for(int i=0;i<8;i++)frame[2+i]=(uint8_t)((uint64_t)len>>(56-8*i));
 uint8_t *mask=frame+hdr-4;if(RAND_bytes(mask,4)!=1){free(frame);return EIO;}
 for(size_t i=0;i<len;i++)frame[hdr+i]=payload[i]^mask[i&3];
 int err=net_write(b,frame,hdr+len);free(frame);return err;
}
static int consume_frames(bridge *b){
 while(b->rx_len>=2){uint8_t *f=b->rx;uint8_t op=f[0]&15;
  if((f[0]&0x70)||(f[1]&0x80))return EPROTO;
  size_t hdr=2,n=f[1]&127;
  if(n==126){if(b->rx_len<4)return 0;n=((size_t)f[2]<<8)|f[3];hdr=4;}
  else if(n==127){if(b->rx_len<10)return 0;uint64_t wide=0;
   for(int i=0;i<8;i++)wide=(wide<<8)|f[2+i];if(wide>8u*1024u*1024u)return EMSGSIZE;
   n=(size_t)wide;hdr=10;}
  if(n>8u*1024u*1024u||b->rx_len<hdr+n)return n>8u*1024u*1024u?EMSGSIZE:0;
  int err=0;if(op==2||op==0)err=write_all_fd(b->local,f+hdr,n);
  else if(op==9)err=send_frame(b,10,f+hdr,n);
  else if(op==8)return ECONNRESET;
  else if(op!=10)return EPROTO;
  if(err)return err;memmove(b->rx,b->rx+hdr+n,b->rx_len-hdr-n);b->rx_len-=hdr+n;
 }
 return 0;
}
static void *worker(void *arg){bridge *b=arg;
 if(open_network(b)||upgrade(b)){cleanup(b);return NULL;}
 if(consume_frames(b)){cleanup(b);return NULL;}
 for(;;){struct pollfd p[2]={{.fd=b->local,.events=POLLIN},{.fd=b->net,.events=POLLIN}};
  int n=poll(p,2,100);if(n<0){if(errno==EINTR)continue;break;}
  if(p[0].revents&(POLLHUP|POLLERR|POLLNVAL))break;
  if(p[1].revents&(POLLHUP|POLLERR|POLLNVAL))break;
  if(p[0].revents&POLLIN){uint8_t data[65536];ssize_t got=recv(b->local,data,sizeof(data),0);
   if(got<=0||send_frame(b,2,data,(size_t)got))break;}
  if((p[1].revents&POLLIN)||(b->ssl&&SSL_pending(b->ssl)>0)){
   uint8_t data[65536];int got=net_read(b,data,sizeof(data));
   if(got==-EINTR)continue;if(got<=0||append_rx(b,data,(size_t)got)||consume_frames(b))break;}
 }
 cleanup(b);return NULL;
}
int viart_ws_bridge_open(const char *endpoint,const char *token,const char *ca_file){
 if(token){size_t n=strlen(token);if(n>512)return -EINVAL;
  for(size_t i=0;i<n;i++)if((unsigned char)token[i]<33||(unsigned char)token[i]>126)return -EINVAL;}
 bool tls=!strncmp(endpoint,"wss://",6);const char *address=endpoint+(tls?6:5);
 const char *colon=strrchr(address,':');if(!colon||colon==address||(size_t)(colon-address)>=64)return -EINVAL;
 char *end;errno=0;long port=strtol(colon+1,&end,10);if(errno||*end||port<1||port>65535)return -EINVAL;
 int fds[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,fds)<0)return -errno;
 int flags=fcntl(fds[0],F_GETFL,0);if(flags<0||fcntl(fds[0],F_SETFL,flags|O_NONBLOCK)<0){int err=errno;close(fds[0]);close(fds[1]);return -err;}
 struct timeval timeout={.tv_sec=5};(void)setsockopt(fds[1],SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
 bridge *b=calloc(1,sizeof(*b));if(!b){close(fds[0]);close(fds[1]);return -ENOMEM;}
 b->local=fds[1];b->net=-1;b->tls=tls;b->port=(uint16_t)port;
 memcpy(b->host,address,(size_t)(colon-address));b->host[colon-address]=0;
 if(token)b->token=strdup(token);if(ca_file)b->ca=strdup(ca_file);
 if((token&&!b->token)||(ca_file&&!b->ca)){cleanup(b);close(fds[0]);return -ENOMEM;}
 pthread_t thread;int err=pthread_create(&thread,NULL,worker,b);
 if(err){cleanup(b);close(fds[0]);return -err;}
 pthread_detach(thread);return fds[0];
}
