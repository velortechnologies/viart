#define _GNU_SOURCE
#include "protocol.h"
#include "config.h"
#include "acl.h"
#include "auth.h"
#include "index.h"
#ifndef VIART_WITH_WS
#define VIART_WITH_WS 1
#endif
#ifndef VIART_WITH_BROKER_RPC
#define VIART_WITH_BROKER_RPC 1
#endif
#if VIART_WITH_BROKER_RPC
#include "rpc.h"
#include "rpc_wire.h"
#endif
#if VIART_WITH_WS
#include "ws.h"
#endif
#include "subscriptions.h"
#include "wire.h"
#include <errno.h>
#include <limits.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#if VIART_WITH_WS
#include <openssl/ssl.h>
#else
typedef void SSL;
typedef void SSL_CTX;
#endif
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <unistd.h>
typedef struct packet { uint8_t *data; size_t len,pos; struct packet *next; } packet;
typedef struct peer { int fd,stage; bool closing,reject,tcp,ws,ws_ready,ws_cont,tls_want_write,tls_read_want_write,tls_write_want_read,has_bearer; SSL *ssl; uint8_t bearer_digest[32]; uint32_t ipv4,uid,interest; uint64_t born_ms,last_rx_ms; uint16_t name_len; char *name,*primary_name; uint8_t *in,*ws_in; size_t in_len,in_cap,ws_len,ws_cap,queued; packet *head,*tail; bp_sub *subs,*exclusions; uint64_t seen_gen,r_frames,r_bytes,w_frames,w_bytes; struct peer *next; } peer;
typedef struct { int ep,listener,tcp_listener,ws_listener,wss_listener; SSL_CTX *tls_ctx; peer *peers; bp_config cfg; bp_acl_rule *acl; bp_auth_entry *tokens; bp_index index; uint64_t route_gen,start_ms,r_frames,r_bytes,w_frames,w_bytes; size_t count; dev_t socket_dev; ino_t socket_ino; } broker;
static volatile sig_atomic_t run=1;
static void on_signal(int s){(void)s;run=0;}
static uint64_t now_ms(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return (uint64_t)ts.tv_sec*1000+ts.tv_nsec/1000000;}
static int watch(broker *b,peer *p){struct epoll_event e={.events=(p->reject?0:EPOLLIN)|EPOLLRDHUP|
 (p->tls_want_write||p->tls_read_want_write||(p->head&&!p->tls_write_want_read)?EPOLLOUT:0),.data.ptr=p};
 if(e.events==p->interest)return 0;
 if(epoll_ctl(b->ep,EPOLL_CTL_MOD,p->fd,&e)<0)return -1;
 p->interest=e.events;return 0;}
static int tls_handshake(broker *b,peer *p){
#if VIART_WITH_WS
 if(p->stage!=-2)return 0;int n=SSL_accept(p->ssl);
 if(n==1){p->stage=-1;p->tls_want_write=false;return watch(b,p)<0?errno:0;}
 int err=SSL_get_error(p->ssl,n);
 if(err==SSL_ERROR_WANT_READ||err==SSL_ERROR_WANT_WRITE){p->tls_want_write=err==SSL_ERROR_WANT_WRITE;return watch(b,p)<0?errno:0;}
 return EPROTO;
#else
 (void)b;(void)p;return EOPNOTSUPP;
#endif
}
#if VIART_WITH_WS
static ssize_t peer_recv(peer *p,void *data,size_t len){
 if(!p->ssl)return recv(p->fd,data,len,0);
 int n=SSL_read(p->ssl,data,(int)(len>INT_MAX?INT_MAX:len));
 if(n>0){p->tls_read_want_write=false;return n;}
 int err=SSL_get_error(p->ssl,n);
 if(err==SSL_ERROR_WANT_READ||err==SSL_ERROR_WANT_WRITE){p->tls_read_want_write=err==SSL_ERROR_WANT_WRITE;errno=EAGAIN;return -1;}
 if(err==SSL_ERROR_ZERO_RETURN)return 0;errno=EIO;return -1;
}
#endif
static ssize_t peer_send(peer *p,const void *data,size_t len){
#if VIART_WITH_WS
 if(!p->ssl)return send(p->fd,data,len,MSG_NOSIGNAL);
 int n=SSL_write(p->ssl,data,(int)(len>INT_MAX?INT_MAX:len));
 if(n>0){p->tls_write_want_read=false;return n;}
 int err=SSL_get_error(p->ssl,n);
 if(err==SSL_ERROR_WANT_READ||err==SSL_ERROR_WANT_WRITE){p->tls_write_want_read=err==SSL_ERROR_WANT_READ;errno=EAGAIN;return -1;}
 errno=EIO;return -1;
#else
 return send(p->fd,data,len,MSG_NOSIGNAL);
#endif
}
static ssize_t peer_send_batch(peer *p,size_t budget){
 struct iovec iov[16];size_t count=0;
 for(packet *q=p->head;q&&count<16&&budget;q=q->next){
  size_t len=q->len-q->pos;if(len>budget)len=budget;
  iov[count++]=(struct iovec){.iov_base=q->data+q->pos,.iov_len=len};budget-=len;
 }
 struct msghdr msg={.msg_iov=iov,.msg_iovlen=count};
 return sendmsg(p->fd,&msg,MSG_NOSIGNAL);
}
static int enqueue_raw(broker *b,peer *p,uint8_t *data,size_t len){
 if(!data)return ENOMEM;
 if(len>b->cfg.max_queue-p->queued){free(data);p->closing=true;return ENOBUFS;}
 packet *q=calloc(1,sizeof(*q));if(!q){free(data);return ENOMEM;}
 q->data=data;q->len=len;if(p->tail)p->tail->next=q;else p->head=q;p->tail=q;p->queued+=len;
 if(p->stage==3){p->w_frames++;p->w_bytes+=len;b->w_frames++;b->w_bytes+=len;}
 if(watch(b,p)<0){p->closing=true;return errno;}return 0;
}
static int enqueue(broker *b,peer *p,uint8_t *data,size_t len){
#if VIART_WITH_WS
 if(p->ws_ready){uint8_t *wrapped=NULL;size_t wrapped_len=0;
  int err=bp_ws_encode(2,data,len,&wrapped,&wrapped_len);free(data);if(err)return err;
  return enqueue_raw(b,p,wrapped,wrapped_len);}
#endif
 return enqueue_raw(b,p,data,len);
}
static int enbyte(broker *b,peer *p,uint8_t value){uint8_t *x=malloc(1);if(!x)return ENOMEM;*x=value;return enqueue(b,p,x,1);}
static int ack(broker *b,peer *p,uint32_t id,uint8_t code){size_t n=0;uint8_t *x=bp_ack(id,code,&n);return enqueue(b,p,x,n);}
static void consume(peer *p,size_t n){p->in_len-=n;memmove(p->in,p->in+n,p->in_len);}
static peer *byname(broker *b,const uint8_t *name,size_t n){for(peer *p=b->peers;p;p=p->next)if(p->stage==3&&!p->closing&&strlen(p->name)==n&&!memcmp(p->name,name,n))return p;return NULL;}
static int subs(broker *b,peer *p,const bp_command *c){
 if(b->cfg.acl_path&&(c->op==BP_SUBSCRIBE||c->op==BP_EXCLUDE)){
  size_t check=0;while(check<c->body_len){const uint8_t *end=memchr(c->body+check,0,c->body_len-check);
   size_t n=end?(size_t)(end-c->body-check):c->body_len-check;
   if(!n||n>4096)return EPROTO;char *s=strndup((const char *)c->body+check,n);if(!s)return ENOMEM;
   bool allowed=bp_acl_allowed(b->acl,p->primary_name?p->primary_name:p->name,BP_ACL_SUBSCRIBE,s,p->ipv4,!p->tcp,p->uid);
   free(s);if(!allowed)return EACCES;check+=n+(end?1:0);
  }
 }
 size_t pos=0;while(pos<c->body_len){const uint8_t *end=memchr(c->body+pos,0,c->body_len-pos);size_t n=end?(size_t)(end-c->body-pos):c->body_len-pos;
  if(!n||n>4096)return EPROTO;char *s=strndup((const char *)c->body+pos,n);if(!s)return ENOMEM;
  int err=0;if(c->op==BP_SUBSCRIBE){err=bp_sub_add(&p->subs,s);if(!err){err=bp_index_add(&b->index,s,p);if(err)bp_sub_remove(&p->subs,s);}}
  else if(c->op==BP_UNSUBSCRIBE){bp_sub_remove(&p->subs,s);bp_index_remove(&b->index,s,p);}
  else if(c->op==BP_EXCLUDE)err=bp_sub_add(&p->exclusions,s);
  else bp_sub_remove(&p->exclusions,s);
  free(s);if(err)return err;pos+=n+(end?1:0);
 }return 0;
}
typedef struct {broker *b; peer *from; const bp_command *command; const char *topic; const uint8_t *receiver,*payload; size_t receiver_len,payload_len; uint8_t *code; const char *sender; uint64_t generation;} route_context;
static void publish_to(void *owner,void *context){
 peer *to=owner;route_context *ctx=context;
 if(to->stage!=3||to->closing||to->seen_gen==ctx->generation||bp_sub_match(to->exclusions,ctx->topic))return;
 if(ctx->receiver){const char *primary=to->primary_name?to->primary_name:to->name;
  if(strlen(primary)!=ctx->receiver_len||memcmp(primary,ctx->receiver,ctx->receiver_len))return;}
 to->seen_gen=ctx->generation;
 size_t n=0;uint8_t *data=bp_delivery(BP_PUBLISH,ctx->command->qos&2,ctx->sender,
  ctx->command->target,ctx->command->target_len,ctx->payload,ctx->payload_len,&n);
 (void)enqueue(ctx->b,to,data,n);
}
#if VIART_WITH_BROKER_RPC
static void announce(broker *b,const char *subject,const char *detail,const char *topic){
 struct timespec ts;clock_gettime(CLOCK_REALTIME,&ts);
 uint8_t *data=NULL;size_t len=0;
 if(bp_rpc_event(subject,detail,(uint64_t)ts.tv_sec*1000000000u+(uint64_t)ts.tv_nsec,&data,&len))return;
 bp_command frame={.op=BP_PUBLISH,.target=(const uint8_t *)topic,.target_len=strlen(topic),
  .payload=data,.payload_len=len};
 uint8_t code=BP_OK;
 route_context ctx={.b=b,.command=&frame,.topic=topic,.payload=data,.payload_len=len,
  .code=&code,.sender=".broker",.generation=++b->route_gen};
 bp_index_visit(&b->index,topic,publish_to,&ctx);
 free(data);
}
static int core_rpc(broker *b,peer *from,const bp_command *c){
 viart_rpc_frame request;
 if(viart_rpc_parse(c->payload,c->payload_len,&request)||request.kind!=VR_REQUEST||!request.id)return 0;
 bp_rpc_client_info *clients=calloc(b->count+1,sizeof(*clients));if(!clients)return ENOMEM;
 size_t count=0;
 clients[count++]=(bp_rpc_client_info){.name=".broker",.kind="internal",.instances=1};
 for(peer *p=b->peers;p;p=p->next){if(p->stage!=3||p->closing||p->primary_name)continue;
  size_t instances=1;for(peer *secondary=b->peers;secondary;secondary=secondary->next)
   if(secondary->stage==3&&!secondary->closing&&secondary->primary_name&&
      !strcmp(secondary->primary_name,p->name))instances++;
  size_t queued=0;for(packet *q=p->head;q;q=q->next)queued++;
  clients[count++]=(bp_rpc_client_info){.name=p->name,.kind=p->ws?"websocket":p->tcp?"tcp":"local_ipc",.port=p->ws?(p->ssl?b->cfg.wss_bind:b->cfg.ws_bind):p->tcp?b->cfg.tcp_bind:b->cfg.socket_path,
   .r_frames=p->r_frames,.r_bytes=p->r_bytes,.w_frames=p->w_frames,.w_bytes=p->w_bytes,
   .queue=queued,.instances=instances};
 }
 bp_rpc_snapshot snapshot={.uptime=(now_ms()-b->start_ms)/1000,.r_frames=b->r_frames,
  .r_bytes=b->r_bytes,.w_frames=b->w_frames,.w_bytes=b->w_bytes,
  .clients=clients,.clients_len=count};
 uint8_t *rpc=NULL;size_t rpc_len=0;int err=bp_rpc_serve(&request,&snapshot,&rpc,&rpc_len);
 free(clients);if(err)return err;
 size_t wire_len=0;uint8_t *wire=bp_delivery(BP_MESSAGE,c->qos&2,".broker",NULL,0,rpc,rpc_len,&wire_len);
 free(rpc);return enqueue(b,from,wire,wire_len);
}
#else
static void announce(broker *b,const char *subject,const char *detail,const char *topic){
 (void)b;(void)subject;(void)detail;(void)topic;
}
#endif
static int route(broker *b,peer *from,const bp_command *c,uint8_t *code){
 if(b->cfg.acl_path){bp_acl_action action=c->op==BP_MESSAGE?BP_ACL_P2P:c->op==BP_BROADCAST?BP_ACL_BROADCAST:BP_ACL_PUBLISH;
  char *target=strndup((const char *)c->target,c->target_len);if(!target)return ENOMEM;
  bool allowed=bp_acl_allowed(b->acl,from->primary_name?from->primary_name:from->name,action,target,from->ipv4,!from->tcp,from->uid);
  free(target);if(!allowed){*code=BP_ERR_ACCESS;return 0;}
 }
 if(c->op==BP_BROADCAST){
  char *mask=strndup((const char *)c->target,c->target_len);if(!mask)return ENOMEM;
  for(peer *to=b->peers;to;to=to->next){if(to->stage!=3||to->closing||!bp_name_match(mask,to->name))continue;
   size_t n=0;uint8_t *data=bp_delivery(BP_BROADCAST,c->qos&2,from->name,NULL,0,c->payload,c->payload_len,&n);
   (void)enqueue(b,to,data,n);
  }free(mask);return 0;
 }
 if(c->op==BP_MESSAGE){
#if VIART_WITH_BROKER_RPC
  if(c->target_len==7&&!memcmp(c->target,".broker",7))return core_rpc(b,from,c);
#else
  if(c->target_len==7&&!memcmp(c->target,".broker",7)){*code=BP_ERR_NOT_REGISTERED;return 0;}
#endif
  peer *to=byname(b,c->target,c->target_len);if(!to){*code=BP_ERR_NOT_REGISTERED;return 0;}
  size_t n=0;uint8_t *data=bp_delivery(BP_MESSAGE,c->qos&2,from->name,NULL,0,c->payload,c->payload_len,&n);
  if(enqueue(b,to,data,n))*code=BP_ERR_NOT_DELIVERED;return 0;}
 char *topic=strndup((const char *)c->target,c->target_len);if(!topic)return ENOMEM;
 const uint8_t *receiver=NULL,*payload=c->payload;size_t receiver_len=0,payload_len=c->payload_len;
 if(c->op==BP_PUBLISH_FOR){const uint8_t *end=memchr(c->payload,0,c->payload_len);
  if(!end||end==c->payload){free(topic);return EPROTO;}
  receiver=c->payload;receiver_len=(size_t)(end-receiver);payload=end+1;payload_len=c->payload_len-receiver_len-1;}
 route_context ctx={.b=b,.from=from,.sender=from->name,.command=c,.topic=topic,.receiver=receiver,
  .receiver_len=receiver_len,.payload=payload,.payload_len=payload_len,.code=code,.generation=++b->route_gen};
 bp_index_visit(&b->index,topic,publish_to,&ctx);
 free(topic);return 0;
}
static int process(broker *b,peer *p);
static int process(broker *b,peer *p){for(;;){
 if(p->stage==0){if(p->in_len<3)return 0;if(p->in[0]!=0xeb||viart_le16(p->in+1)!=1)return EPROTO;consume(p,3);if(enbyte(b,p,BP_OK))return ENOMEM;p->stage=1;}
 else if(p->stage==1){if(p->in_len<2)return 0;p->name_len=viart_le16(p->in);consume(p,2);if(!p->name_len)return EPROTO;p->stage=2;}
 else if(p->stage==2){if(p->in_len<p->name_len)return 0;if(p->in[0]=='.'||memchr(p->in,0,p->name_len))return EPROTO;
  if(byname(b,p->in,p->name_len)){if(enbyte(b,p,BP_ERR_BUSY))return ENOMEM;p->reject=true;p->stage=4;return 0;}
  p->name=strndup((const char *)p->in,p->name_len);if(!p->name)return ENOMEM;consume(p,p->name_len);
  char *secondary=strstr(p->name,"%%");
  if(secondary){
   size_t primary_len=(size_t)(secondary-p->name);
   if(!primary_len||!byname(b,(uint8_t *)p->name,primary_len)){
    if(enbyte(b,p,BP_ERR_NOT_REGISTERED))return ENOMEM;p->reject=true;p->stage=4;return 0;
   }
   p->primary_name=strndup(p->name,primary_len);if(!p->primary_name)return ENOMEM;
  }
  if(b->cfg.acl_path&&!bp_acl_allowed(b->acl,p->primary_name?p->primary_name:p->name,
       BP_ACL_CONNECT,NULL,p->ipv4,!p->tcp,p->uid)){
   if(enbyte(b,p,BP_ERR_ACCESS))return ENOMEM;p->reject=true;p->stage=4;return 0;
  }
#if VIART_WITH_WS
  if(p->ssl&&b->tokens&&(!p->has_bearer||
      !bp_auth_check(b->tokens,p->primary_name?p->primary_name:p->name,p->bearer_digest))){
   if(enbyte(b,p,BP_ERR_ACCESS))return ENOMEM;p->reject=true;p->stage=4;return 0;}
#endif
  if(enbyte(b,p,BP_OK))return ENOMEM;p->stage=3;
  if(!p->primary_name)announce(b,"reg",p->name,".broker/info");}
 else if(p->stage==4)return 0;
 else{bp_command c;size_t used=0;int err=bp_parse(p->in,p->in_len,b->cfg.max_frame,&c,&used);if(err==EAGAIN)return 0;if(err)return err;
  if(c.op){p->r_frames++;p->r_bytes+=c.body_len;b->r_frames++;b->r_bytes+=c.body_len;uint8_t code=BP_OK;if(c.op==BP_SUBSCRIBE||c.op==BP_UNSUBSCRIBE||c.op==BP_EXCLUDE||c.op==BP_UNEXCLUDE)err=subs(b,p,&c);else err=route(b,p,&c,&code);
   if(err==EACCES){code=BP_ERR_ACCESS;err=0;}if(err)return err;if((c.qos&1)&&ack(b,p,c.id,code))return ENOMEM;}
  consume(p,used);
 }
}}
#if VIART_WITH_WS
static int ws_process(broker *b,peer *p){
 if(!p->ws_ready){
  if(p->ws_len>8192)return EMSGSIZE;
  uint8_t *end=memmem(p->ws_in,p->ws_len,"\r\n\r\n",4);if(!end)return 0;
  size_t used=(size_t)(end-p->ws_in)+4;uint8_t *response=NULL;size_t response_len=0;
  if(p->ssl&&b->tokens){char *token=NULL;int auth_err=bp_ws_bearer(p->ws_in,used,&token);
   if(auth_err)return auth_err;bp_auth_digest(token,strlen(token),p->bearer_digest);
   OPENSSL_cleanse(token,strlen(token));free(token);p->has_bearer=true;}
  int err=bp_ws_upgrade(p->ws_in,used,&response,&response_len);if(err)return err;
  if(enqueue_raw(b,p,response,response_len))return ENOBUFS;
  p->ws_ready=true;p->stage=0;
  memmove(p->ws_in,p->ws_in+used,p->ws_len-used);p->ws_len-=used;
  uint8_t *hello=malloc(3);if(!hello)return ENOMEM;memcpy(hello,(uint8_t[]){0xeb,1,0},3);
  if(enqueue(b,p,hello,3))return ENOBUFS;
 }
 while(p->ws_len){size_t used=0,payload_len=0;uint8_t op=0,*payload=NULL;bool fin=false;
  int err=bp_ws_decode(p->ws_in,p->ws_len,b->cfg.max_frame+9,&used,&op,&fin,&payload,&payload_len);
  if(err==EAGAIN)return 0;if(err)return err;
  memmove(p->ws_in,p->ws_in+used,p->ws_len-used);p->ws_len-=used;
  if(op==8){free(payload);return ECONNRESET;}
  if(op==9){uint8_t *pong=NULL;size_t n=0;err=bp_ws_encode(10,payload,payload_len,&pong,&n);
   free(payload);if(err)return err;if(enqueue_raw(b,p,pong,n))return ENOBUFS;continue;}
  if(op==10){free(payload);continue;}
  if(op==2){if(p->ws_cont){free(payload);return EPROTO;}p->ws_cont=!fin;}
  else if(op==0){if(!p->ws_cont){free(payload);return EPROTO;}p->ws_cont=!fin;}
  else{free(payload);return EPROTO;}
  if(payload_len>b->cfg.max_frame+9-p->in_len){free(payload);return EMSGSIZE;}
  if(p->in_len+payload_len>p->in_cap){size_t cap=p->in_cap?p->in_cap:8192;
   while(cap<p->in_len+payload_len)cap*=2;
   uint8_t *next=realloc(p->in,cap);if(!next){free(payload);return ENOMEM;}p->in=next;p->in_cap=cap;}
  if(payload_len)memcpy(p->in+p->in_len,payload,payload_len);p->in_len+=payload_len;free(payload);
  err=process(b,p);if(err)return err;
 }
 return 0;
}
#endif
static int read_peer(broker *b,peer *p){if(p->stage==-2)return tls_handshake(b,p);size_t budget=65536;for(;;){
#if VIART_WITH_WS
 if(p->ws){uint8_t data[8192];size_t want=sizeof(data);if(want>budget)want=budget;
  ssize_t n=peer_recv(p,data,want);
  if(n>0){p->last_rx_ms=now_ms();budget-=(size_t)n;
   size_t limit=b->cfg.max_frame+32;if(limit<8192)limit=8192;
   if(p->ws_len+(size_t)n>limit)return EMSGSIZE;
   if(p->ws_len+(size_t)n>p->ws_cap){size_t cap=p->ws_cap?p->ws_cap:8192;
    while(cap<p->ws_len+(size_t)n)cap*=2;uint8_t *next=realloc(p->ws_in,cap);
    if(!next)return ENOMEM;p->ws_in=next;p->ws_cap=cap;}
   memcpy(p->ws_in+p->ws_len,data,(size_t)n);p->ws_len+=(size_t)n;
   int err=ws_process(b,p);if(err||p->closing)return err?err:ENOBUFS;
   if(!budget)return 0;continue;
  }
  if(n==0)return ECONNRESET;if(errno==EAGAIN||errno==EWOULDBLOCK){if(p->ssl)(void)watch(b,p);return 0;}
  if(errno==EINTR)continue;return errno;
 }
#endif
 if(p->in_cap==p->in_len){size_t cap=p->in_cap?p->in_cap*2:8192;size_t limit=p->stage<3?65538:b->cfg.max_frame+9;if(cap>limit)cap=limit;if(cap<=p->in_cap)return EMSGSIZE;
  uint8_t *next=realloc(p->in,cap);if(!next)return ENOMEM;p->in=next;p->in_cap=cap;}
 size_t want=p->in_cap-p->in_len;if(want>budget)want=budget;
 ssize_t n=recv(p->fd,p->in+p->in_len,want,0);
 if(n>0){p->last_rx_ms=now_ms();p->in_len+=(size_t)n;budget-=(size_t)n;int err=process(b,p);if(err||p->closing)return err?err:ENOBUFS;if(!budget)return 0;}
 else if(n==0)return ECONNRESET;else if(errno==EAGAIN||errno==EWOULDBLOCK)return 0;else if(errno!=EINTR)return errno;
}}
static int write_peer(broker *b,peer *p){if(p->stage==-2)return tls_handshake(b,p);size_t budget=65536;while(p->head&&budget){packet *q=p->head;size_t want=q->len-q->pos;if(want>budget)want=budget;
 ssize_t n=(!p->ssl&&q->next)?peer_send_batch(p,budget):peer_send(p,q->data+q->pos,want);
 if(n>0){size_t left=(size_t)n;p->queued-=left;budget-=left;
  while(left){q=p->head;size_t part=q->len-q->pos;if(part>left)part=left;
   q->pos+=part;left-=part;
   if(q->pos==q->len){p->head=q->next;if(!p->head)p->tail=NULL;free(q->data);free(q);}}
 }
 else if(n<0&&errno==EINTR)continue;else if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;else return n==0?ECONNRESET:errno;
 }if(p->reject&&!p->head)p->closing=true;return watch(b,p)<0?errno:0;}
static void drop(broker *b,peer *p){
 if(p->name&&!p->primary_name)announce(b,"unreg",p->name,".broker/info");
 if(p->name&&!p->primary_name)for(peer *other=b->peers;other;other=other->next)
  if(other!=p&&other->primary_name&&!strcmp(other->primary_name,p->name))other->closing=true;
 bp_index_drop_owner(&b->index,p);epoll_ctl(b->ep,EPOLL_CTL_DEL,p->fd,NULL);
#if VIART_WITH_WS
 if(p->ssl)SSL_free(p->ssl);
#endif
 close(p->fd);b->count--;peer **ref=&b->peers;while(*ref&&*ref!=p)ref=&(*ref)->next;if(*ref)*ref=p->next;
 free(p->name);free(p->primary_name);free(p->in);free(p->ws_in);bp_sub_free(p->subs);bp_sub_free(p->exclusions);while(p->head){packet *next=p->head->next;free(p->head->data);free(p->head);p->head=next;}free(p);}
static int accept_all(broker *b,int listener,bool tcp,bool ws,bool tls){for(;;){struct sockaddr_in remote; socklen_t remote_len=sizeof(remote);
 int fd=accept4(listener,tcp?(struct sockaddr *)&remote:NULL,tcp?&remote_len:NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);if(fd<0)return errno==EAGAIN||errno==EWOULDBLOCK?0:errno;
 if(b->count>=b->cfg.max_clients){close(fd);continue;}
 if(tcp){int one=1;(void)setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));}
 peer *p=calloc(1,sizeof(*p));if(!p){close(fd);return ENOMEM;}p->fd=fd;p->tcp=tcp;p->ws=ws;p->stage=tls?-2:ws?-1:0;p->ipv4=tcp?ntohl(remote.sin_addr.s_addr):0;p->born_ms=p->last_rx_ms=now_ms();p->next=b->peers;b->peers=p;b->count++;
#if VIART_WITH_WS
 if(tls){p->ssl=SSL_new(b->tls_ctx);if(!p->ssl||SSL_set_fd(p->ssl,fd)!=1){drop(b,p);return EPROTO;}SSL_set_accept_state(p->ssl);}
#else
 (void)tls;
#endif
 if(!tcp){struct ucred cred;socklen_t len=sizeof(cred);
  if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&cred,&len)<0||len!=sizeof(cred)){drop(b,p);return EACCES;}
  p->uid=(uint32_t)cred.uid;}
 struct epoll_event e={.events=EPOLLIN|EPOLLRDHUP,.data.ptr=p};if(epoll_ctl(b->ep,EPOLL_CTL_ADD,fd,&e)<0){drop(b,p);return errno;}
 p->interest=e.events;
 if(!ws){uint8_t *hello=malloc(3);if(!hello){drop(b,p);return ENOMEM;}memcpy(hello,(uint8_t[]){0xeb,1,0},3);
  if(enqueue(b,p,hello,3)){drop(b,p);return ENOMEM;}}
}}
static void unlink_owned(const broker *b){
 if(!b->socket_ino)return;
 struct stat st;
 if(lstat(b->cfg.socket_path,&st)==0&&S_ISSOCK(st.st_mode)&&
    st.st_dev==b->socket_dev&&st.st_ino==b->socket_ino)unlink(b->cfg.socket_path);
}
int main(int argc,char **argv){
 broker b={.ep=-1,.listener=-1,.tcp_listener=-1,.ws_listener=-1,.wss_listener=-1};
 if(bp_config_parse(argc,argv,&b.cfg)){fprintf(stderr,"usage: %s [-B /path/to/socket] [--tcp IPv4:PORT] [--ws IPv4:PORT] [--wss IPv4:PORT --tls-cert PEM --tls-key PEM] [--acl path] [--max-clients N] [--max-frame BYTES] [--max-queue BYTES] [--handshake-ms N] [--idle-ms N]\n",argv[0]);return 2;}
 if(!VIART_WITH_WS&&(b.cfg.ws_bind||b.cfg.wss_bind||b.cfg.tokens_path)){
  fprintf(stderr,"this build does not support WebSocket or TLS\n");return 2;}
 if(b.cfg.acl_path){int err=bp_acl_load(b.cfg.acl_path,&b.acl);if(err){fprintf(stderr,"ACL load failed: %s\n",strerror(err));return 2;}}
#if VIART_WITH_WS
 if(b.cfg.tokens_path){int err=bp_auth_load(b.cfg.tokens_path,&b.tokens);if(err){fprintf(stderr,"token map load failed: %s\n",strerror(err));bp_acl_free(b.acl);return 2;}}
 if(b.cfg.wss_bind){b.tls_ctx=SSL_CTX_new(TLS_server_method());if(!b.tls_ctx)return 2;
  SSL_CTX_set_min_proto_version(b.tls_ctx,TLS1_2_VERSION);
  if(SSL_CTX_use_certificate_file(b.tls_ctx,b.cfg.tls_cert,SSL_FILETYPE_PEM)!=1||
     SSL_CTX_use_PrivateKey_file(b.tls_ctx,b.cfg.tls_key,SSL_FILETYPE_PEM)!=1||
     SSL_CTX_check_private_key(b.tls_ctx)!=1){fprintf(stderr,"TLS certificate/key load failed\n");SSL_CTX_free(b.tls_ctx);return 2;}}
#endif
 signal(SIGTERM,on_signal);signal(SIGINT,on_signal);b.start_ms=now_ms();b.ep=epoll_create1(EPOLL_CLOEXEC);if(b.ep<0){perror("epoll");return 1;}
 if(b.cfg.socket_path){struct sockaddr_un addr={.sun_family=AF_UNIX};
  if(strlen(b.cfg.socket_path)>=sizeof(addr.sun_path)){fprintf(stderr,"socket path too long\n");close(b.ep);return 1;}
  strcpy(addr.sun_path,b.cfg.socket_path);
  b.listener=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
  if(b.listener<0)goto startup_fail;
  if(bind(b.listener,(struct sockaddr *)&addr,sizeof(addr))<0)goto startup_fail;
  struct stat st;if(lstat(b.cfg.socket_path,&st)==0){b.socket_dev=st.st_dev;b.socket_ino=st.st_ino;}
  if(listen(b.listener,128)<0)goto startup_fail;
  struct epoll_event e={.events=EPOLLIN,.data.ptr=NULL};
  if(epoll_ctl(b.ep,EPOLL_CTL_ADD,b.listener,&e)<0)goto startup_fail;}
 if(b.cfg.tcp_bind){const char *colon=strrchr(b.cfg.tcp_bind,':');char host[64];
  if(!colon||colon==b.cfg.tcp_bind||(size_t)(colon-b.cfg.tcp_bind)>=sizeof(host))goto startup_fail;
  memcpy(host,b.cfg.tcp_bind,(size_t)(colon-b.cfg.tcp_bind));host[colon-b.cfg.tcp_bind]=0;
  char *end;errno=0;long port=strtol(colon+1,&end,10);if(errno||*end||port<1||port>65535)goto startup_fail;
  struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons((uint16_t)port)};
  if(inet_pton(AF_INET,host,&addr.sin_addr)!=1)goto startup_fail;
  if(!b.cfg.allow_public_tcp&&((ntohl(addr.sin_addr.s_addr)>>24)!=127)){
   fprintf(stderr,"non-loopback TCP requires --allow-public-tcp (raw TCP has no peer authentication)\n");goto startup_fail;}
  b.tcp_listener=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
  if(b.tcp_listener<0)goto startup_fail;
  if(bind(b.tcp_listener,(struct sockaddr *)&addr,sizeof(addr))<0||listen(b.tcp_listener,128)<0)goto startup_fail;
  struct epoll_event e={.events=EPOLLIN,.data.ptr=&b.tcp_listener};
  if(epoll_ctl(b.ep,EPOLL_CTL_ADD,b.tcp_listener,&e)<0)goto startup_fail;}
#if VIART_WITH_WS
 if(b.cfg.ws_bind){const char *colon=strrchr(b.cfg.ws_bind,':');char host[64];
  if(!colon||colon==b.cfg.ws_bind||(size_t)(colon-b.cfg.ws_bind)>=sizeof(host))goto startup_fail;
  memcpy(host,b.cfg.ws_bind,(size_t)(colon-b.cfg.ws_bind));host[colon-b.cfg.ws_bind]=0;
  char *end;errno=0;long port=strtol(colon+1,&end,10);if(errno||*end||port<1||port>65535)goto startup_fail;
  struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons((uint16_t)port)};
  if(inet_pton(AF_INET,host,&addr.sin_addr)!=1)goto startup_fail;
  if(!b.cfg.allow_public_tcp&&((ntohl(addr.sin_addr.s_addr)>>24)!=127)){
   fprintf(stderr,"non-loopback WS requires --allow-public-tcp (cleartext transport)\n");goto startup_fail;}
  b.ws_listener=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
  if(b.ws_listener<0)goto startup_fail;
  if(bind(b.ws_listener,(struct sockaddr *)&addr,sizeof(addr))<0||listen(b.ws_listener,128)<0)goto startup_fail;
  struct epoll_event e={.events=EPOLLIN,.data.ptr=&b.ws_listener};
  if(epoll_ctl(b.ep,EPOLL_CTL_ADD,b.ws_listener,&e)<0)goto startup_fail;}
 if(b.cfg.wss_bind){const char *colon=strrchr(b.cfg.wss_bind,':');char host[64];
  if(!colon||colon==b.cfg.wss_bind||(size_t)(colon-b.cfg.wss_bind)>=sizeof(host))goto startup_fail;
  memcpy(host,b.cfg.wss_bind,(size_t)(colon-b.cfg.wss_bind));host[colon-b.cfg.wss_bind]=0;
  char *end;errno=0;long port=strtol(colon+1,&end,10);if(errno||*end||port<1||port>65535)goto startup_fail;
  struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons((uint16_t)port)};
  if(inet_pton(AF_INET,host,&addr.sin_addr)!=1)goto startup_fail;
  if((ntohl(addr.sin_addr.s_addr)>>24)!=127&&!b.tokens){
   fprintf(stderr,"non-loopback WSS requires --tokens (TLS alone does not authenticate clients)\n");goto startup_fail;}
  b.wss_listener=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(b.wss_listener<0)goto startup_fail;
  if(bind(b.wss_listener,(struct sockaddr *)&addr,sizeof(addr))<0||listen(b.wss_listener,128)<0)goto startup_fail;
  struct epoll_event e={.events=EPOLLIN,.data.ptr=&b.wss_listener};
  if(epoll_ctl(b.ep,EPOLL_CTL_ADD,b.wss_listener,&e)<0)goto startup_fail;}
#endif
 while(run){struct epoll_event events[64];int n=epoll_wait(b.ep,events,64,200);if(n<0){if(errno==EINTR)continue;perror("epoll_wait");break;}
  for(int i=0;i<n;i++){peer *p=events[i].data.ptr;if(!p||p==(void *)&b.tcp_listener||p==(void *)&b.ws_listener||p==(void *)&b.wss_listener){
   int listener=!p?b.listener:p==(void *)&b.tcp_listener?b.tcp_listener:p==(void *)&b.ws_listener?b.ws_listener:b.wss_listener;
   if(accept_all(&b,listener,p!=NULL,p==(void *)&b.ws_listener||p==(void *)&b.wss_listener,p==(void *)&b.wss_listener))perror("accept");continue;}
   if(p->closing){drop(&b,p);continue;}
   int err=0;if((events[i].events&EPOLLIN)||(p->tls_read_want_write&&(events[i].events&EPOLLOUT)))err=read_peer(&b,p);
   if(!err&&((events[i].events&EPOLLOUT)||(p->tls_write_want_read&&(events[i].events&EPOLLIN)))&&
      (p->head||p->stage==-2))err=write_peer(&b,p);
   if(err||p->closing||(events[i].events&(EPOLLERR|EPOLLHUP|EPOLLRDHUP)))drop(&b,p);
  }peer *p=b.peers;uint64_t now=now_ms();while(p){peer *next=p->next;
   if(p->stage<3&&now-p->born_ms>b.cfg.handshake_ms)p->closing=true;
   if(p->stage==3&&now-p->last_rx_ms>b.cfg.idle_ms)p->closing=true;
   if(p->closing)drop(&b,p);p=next;}
 }
 announce(&b,"shutdown",NULL,".broker/warn");
 for(peer *p=b.peers;p;p=p->next)(void)write_peer(&b,p);
 while(b.peers)drop(&b,b.peers);bp_index_clear(&b.index);bp_acl_free(b.acl);
#if VIART_WITH_WS
 bp_auth_free(b.tokens);
#endif
 if(b.listener>=0)close(b.listener);if(b.tcp_listener>=0)close(b.tcp_listener);if(b.ws_listener>=0)close(b.ws_listener);if(b.wss_listener>=0)close(b.wss_listener);
#if VIART_WITH_WS
 if(b.tls_ctx)SSL_CTX_free(b.tls_ctx);
#endif
 close(b.ep);
 unlink_owned(&b);return 0;
startup_fail:
 perror("socket/bind/listen/epoll_ctl");
 if(b.listener>=0)close(b.listener);
 if(b.tcp_listener>=0)close(b.tcp_listener);
 if(b.ws_listener>=0)close(b.ws_listener);
 if(b.wss_listener>=0)close(b.wss_listener);
#if VIART_WITH_WS
 if(b.tls_ctx)SSL_CTX_free(b.tls_ctx);
 bp_auth_free(b.tokens);
#endif
 close(b.ep);
 bp_acl_free(b.acl);
 unlink_owned(&b);
 return 1;
}
