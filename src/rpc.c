#define _POSIX_C_SOURCE 200809L
#include "viart_rpc.h"
#include "internal.h"
#include "rpc_wire.h"
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct rpc_pending {
    uint32_t id;
    char *target;
    int64_t deadline_ms;
    struct rpc_pending *next;
} rpc_pending;
struct viart_rpc {
    viart_client *transport;
    viart_rpc_event_fn on_event;
    void *user;
    pthread_mutex_t mutex;
    rpc_pending *pending;
    uint32_t next_id, timeout_ms;
};
static int64_t now_ms(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return (int64_t)ts.tv_sec*1000+ts.tv_nsec/1000000;}
static void emit(viart_rpc *r,const viart_rpc_event *e){if(r->on_event)r->on_event(r,e,r->user);}
static void fail_list(viart_rpc *r,rpc_pending *list,int error){
 while(list){rpc_pending *next=list->next;
  viart_rpc_event ev={.kind=error==ETIMEDOUT?VIART_RPC_TIMEOUT:VIART_RPC_ERROR,
   .id=list->id,.error=error,.sender={(const uint8_t *)list->target,strlen(list->target)}};
  emit(r,&ev);free(list->target);free(list);list=next;
 }
}
static rpc_pending *detach_matching(viart_rpc *r,uint32_t id,viart_bytes sender){
 pthread_mutex_lock(&r->mutex);
 rpc_pending **ref=&r->pending,*found=NULL;
 while(*ref){rpc_pending *p=*ref;
  if(p->id==id&&strlen(p->target)==sender.len&&!memcmp(p->target,sender.data,sender.len)){
   *ref=p->next;p->next=NULL;found=p;break;}
  ref=&p->next;
 }
 pthread_mutex_unlock(&r->mutex);
 return found;
}
static void on_tick(viart_client *transport,void *user){
 (void)transport;viart_rpc *r=user;int64_t now=now_ms();rpc_pending *expired=NULL;
 pthread_mutex_lock(&r->mutex);
 rpc_pending **ref=&r->pending;
 while(*ref){rpc_pending *p=*ref;if(p->deadline_ms<=now){*ref=p->next;p->next=expired;expired=p;}
  else ref=&p->next;}
 pthread_mutex_unlock(&r->mutex);
 fail_list(r,expired,ETIMEDOUT);
}
static void on_transport(viart_client *transport,const viart_event *event,void *user){
 (void)transport;viart_rpc *r=user;
 if(event->kind==VIART_CONNECTED){viart_rpc_event ev={.kind=VIART_RPC_CONNECTED};emit(r,&ev);return;}
 if(event->kind==VIART_DISCONNECTED||event->kind==VIART_CONNECT_ERROR){
  rpc_pending *list=NULL;
  pthread_mutex_lock(&r->mutex);list=r->pending;r->pending=NULL;pthread_mutex_unlock(&r->mutex);
  fail_list(r,list,ENOTCONN);
  viart_rpc_event ev={.kind=VIART_RPC_DISCONNECTED,.error=event->error};emit(r,&ev);return;
 }
 if(event->kind!=VIART_MESSAGE)return;
 if(event->frame_kind!=0x12){viart_rpc_event ev={.kind=VIART_RPC_FRAME,.frame=event};emit(r,&ev);return;}
 viart_rpc_frame frame;
 if(viart_rpc_parse(event->payload.data,event->payload.len,&frame))return;
 viart_rpc_event ev={.id=frame.id,.rpc_error_code=frame.error_code,.sender=event->sender,
  .method={frame.method,frame.method_len},.payload={frame.payload,frame.payload_len}};
 if(frame.kind==VR_NOTIFICATION)ev.kind=VIART_RPC_NOTIFICATION;
 else if(frame.kind==VR_REQUEST)ev.kind=VIART_RPC_REQUEST;
 else if(frame.kind==VR_REPLY||frame.kind==VR_ERROR){
  rpc_pending *p=detach_matching(r,frame.id,event->sender);
  if(!p)return;
  ev.kind=frame.kind==VR_REPLY?VIART_RPC_REPLY:VIART_RPC_ERROR;
  emit(r,&ev);free(p->target);free(p);return;
 }else return;
 emit(r,&ev);
}
int viart_rpc_create(const viart_rpc_options *options,viart_rpc **out){
 if(!options||!out||!options->endpoint||!options->name)return EINVAL;
 viart_rpc *r=calloc(1,sizeof(*r));if(!r)return ENOMEM;
 int err=pthread_mutex_init(&r->mutex,NULL);if(err){free(r);return err;}
 r->on_event=options->on_event;r->user=options->user;
 r->timeout_ms=options->call_timeout_ms?options->call_timeout_ms:5000;
 viart_client_options base={.endpoint=options->endpoint,.name=options->name,.on_event=on_transport,.user=r,
  .bearer_token=options->bearer_token,.tls_ca_file=options->tls_ca_file,
  .max_queued_bytes=options->max_queued_bytes,.max_frame_bytes=options->max_frame_bytes,
  .reconnect_ms=options->reconnect_ms};
 err=viart_client_create(&base,&r->transport);
 if(err){pthread_mutex_destroy(&r->mutex);free(r);return err;}
 r->transport->on_tick=on_tick;r->transport->tick_user=r;
 *out=r;return 0;
}
int viart_rpc_start(viart_rpc *r){return r?viart_client_start(r->transport):EINVAL;}
void viart_rpc_stop(viart_rpc *r){if(r)viart_client_stop(r->transport);}
void viart_rpc_destroy(viart_rpc *r){
 if(!r)return;viart_client_destroy(r->transport);
 pthread_mutex_lock(&r->mutex);rpc_pending *list=r->pending;r->pending=NULL;pthread_mutex_unlock(&r->mutex);
 while(list){rpc_pending *next=list->next;free(list->target);free(list);list=next;}
 pthread_mutex_destroy(&r->mutex);free(r);
}
viart_client *viart_rpc_transport(viart_rpc *r){return r?r->transport:NULL;}
static int send_data(viart_rpc *r,const char *target,uint8_t *data,size_t len,uint8_t qos){
 if(!r||!target||!*target){free(data);return EINVAL;}
 int err=viart_client_send(r->transport,target,data,len,qos,NULL);free(data);return err;
}
int viart_rpc_call(viart_rpc *r,const char *target,const char *method,
                   const void *params,size_t len,uint8_t qos,uint32_t *call_id){
 if(!r||!target||!*target||!method||!*method)return EINVAL;
 rpc_pending *p=calloc(1,sizeof(*p));if(!p)return ENOMEM;
 p->target=strdup(target);if(!p->target){free(p);return ENOMEM;}
 pthread_mutex_lock(&r->mutex);
 for(;;){r->next_id++;if(!r->next_id)r->next_id=1;
  bool used=false;for(rpc_pending *q=r->pending;q;q=q->next)if(q->id==r->next_id){used=true;break;}
  if(!used)break;}
 p->id=r->next_id;p->deadline_ms=now_ms()+r->timeout_ms;p->next=r->pending;r->pending=p;
 uint32_t id=p->id;
 pthread_mutex_unlock(&r->mutex);
 uint8_t *data=NULL;size_t data_len=0;
 int err=viart_rpc_encode_request(id,method,params,len,&data,&data_len);
 if(!err)err=send_data(r,target,data,data_len,qos);
 if(err){rpc_pending *removed=detach_matching(r,id,(viart_bytes){(const uint8_t *)target,strlen(target)});
  if(removed){free(removed->target);free(removed);}return err;}
 if(call_id)*call_id=id;
 return 0;
}
int viart_rpc_call0(viart_rpc *r,const char *target,const char *method,
                    const void *params,size_t len,uint8_t qos){
 uint8_t *data=NULL;size_t data_len=0;
 int err=viart_rpc_encode_request(0,method,params,len,&data,&data_len);
 return err?err:send_data(r,target,data,data_len,qos);
}
int viart_rpc_notify(viart_rpc *r,const char *target,const void *data,size_t len,uint8_t qos){
 uint8_t *encoded=NULL;size_t encoded_len=0;
 int err=viart_rpc_encode_notification(data,len,&encoded,&encoded_len);
 return err?err:send_data(r,target,encoded,encoded_len,qos);
}
static int respond(viart_rpc *r,const char *target,uint8_t kind,uint32_t id,int16_t code,
                   const void *data,size_t len,uint8_t qos){
 if(!id)return EINVAL;uint8_t *encoded=NULL;size_t encoded_len=0;
 int err=viart_rpc_encode_reply(kind,id,code,data,len,&encoded,&encoded_len);
 return err?err:send_data(r,target,encoded,encoded_len,qos);
}
int viart_rpc_reply(viart_rpc *r,const char *target,uint32_t id,
                    const void *data,size_t len,uint8_t qos){
 return respond(r,target,VR_REPLY,id,0,data,len,qos);
}
int viart_rpc_error(viart_rpc *r,const char *target,uint32_t id,int16_t code,
                    const void *data,size_t len,uint8_t qos){
 return respond(r,target,VR_ERROR,id,code,data,len,qos);
}
