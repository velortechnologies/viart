#define _POSIX_C_SOURCE 200809L
#include "common.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint64_t cli_now_ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}
void cli_state_init(cli_state *s) {
    *s=(cli_state){0};
    atomic_init(&s->fast_messages,0);
    pthread_mutex_init(&s->mutex,NULL);
    pthread_cond_init(&s->cond,NULL);
}
void cli_state_fini(cli_state *s) {
    free(s->reply_data);
    pthread_cond_destroy(&s->cond);
    pthread_mutex_destroy(&s->mutex);
}
static void show_frame(const viart_event *e) {
    printf("frame=0x%02x from=%.*s",e->frame_kind,(int)e->sender.len,e->sender.data);
    if(e->topic.len)printf(" topic=%.*s",(int)e->topic.len,e->topic.data);
    putchar('\n');cli_print_payload(e->payload.data,e->payload.len,false);puts("----");fflush(stdout);
}
void cli_raw_event(viart_client *c,const viart_event *e,void *user) {
    (void)c;cli_state *s=user;
    if(e->kind==VIART_MESSAGE && s->listen)show_frame(e);
    if(e->kind==VIART_MESSAGE&&s->fast_count){
        atomic_fetch_add_explicit(&s->fast_messages,1,memory_order_relaxed);
        return;
    }
    if(s->verbose&&(e->kind==VIART_CONNECTED||e->kind==VIART_DISCONNECTED||
       e->kind==VIART_CONNECT_ERROR||e->kind==VIART_ACK))
        fprintf(stderr,"viart-cli: event=%d error=%d ack=%u result=0x%02x\n",
                e->kind,e->error,e->id,e->result);
    pthread_mutex_lock(&s->mutex);
    switch(e->kind) {
    case VIART_CONNECTED:s->connected=true;s->failed=false;break;
    case VIART_DISCONNECTED:case VIART_CONNECT_ERROR:s->connected=false;s->failed=true;break;
    case VIART_ACK:s->acks++;s->ack_result=e->result;break;
    case VIART_MESSAGE:s->messages++;break;
    }
    pthread_cond_broadcast(&s->cond);pthread_mutex_unlock(&s->mutex);
}
void cli_rpc_event(viart_rpc *r,const viart_rpc_event *e,void *user) {
    cli_state *s=user;
    if(s->verbose&&(e->kind==VIART_RPC_CONNECTED||e->kind==VIART_RPC_DISCONNECTED||
       e->kind==VIART_RPC_ERROR||e->kind==VIART_RPC_TIMEOUT))
        fprintf(stderr,"viart-cli: rpc_event=%d error=%d code=%d\n",
                e->kind,e->error,e->rpc_error_code);
    if(e->kind==VIART_RPC_REQUEST) {
        if(s->rpc_listen) {
            printf("RPC call method=%.*s from=%.*s\n",(int)e->method.len,e->method.data,
                   (int)e->sender.len,e->sender.data);
            cli_print_payload(e->payload.data,e->payload.len,false);puts("----");fflush(stdout);
        }
        char *target=strndup((const char *)e->sender.data,e->sender.len);
        if(target) {
            const void *data=NULL;size_t len=0;
            if(e->method.len==18 && !memcmp(e->method.data,"benchmark.selftest",18))
                data=e->payload.data,len=e->payload.len;
            if(e->id)viart_rpc_reply(r,target,e->id,data,len,VIART_QOS_NO);
            free(target);
        }
    } else if(e->kind==VIART_RPC_NOTIFICATION && s->rpc_listen) {
        printf("RPC notification from=%.*s\n",(int)e->sender.len,e->sender.data);
        cli_print_payload(e->payload.data,e->payload.len,false);puts("----");fflush(stdout);
    } else if(e->kind==VIART_RPC_FRAME && s->rpc_listen)show_frame(e->frame);
    pthread_mutex_lock(&s->mutex);
    switch(e->kind) {
    case VIART_RPC_CONNECTED:s->connected=true;s->failed=false;break;
    case VIART_RPC_DISCONNECTED:s->connected=false;s->failed=true;break;
    case VIART_RPC_REPLY:case VIART_RPC_ERROR:case VIART_RPC_TIMEOUT:
        free(s->reply_data);s->reply_data=NULL;s->reply_len=0;
        if(e->payload.len){s->reply_data=malloc(e->payload.len);
            if(s->reply_data){memcpy(s->reply_data,e->payload.data,e->payload.len);s->reply_len=e->payload.len;}}
        s->reply_id=e->id;s->rpc_error=e->kind==VIART_RPC_ERROR?e->rpc_error_code:
            e->kind==VIART_RPC_TIMEOUT?-1:0;
        s->replies++;break;
    default:break;
    }
    pthread_cond_broadcast(&s->cond);pthread_mutex_unlock(&s->mutex);
}
int cli_open_raw(const cli_options *o,cli_state *s,viart_client **out) {
    s->verbose=o->verbose&&!o->silent;
    viart_client_options co={.endpoint=o->endpoint,.name=o->name,.on_event=cli_raw_event,
        .user=s,.bearer_token=o->token,.tls_ca_file=o->ca};
    int err=viart_client_create(&co,out);if(err)return err;
    err=viart_client_start(*out);if(err){viart_client_destroy(*out);*out=NULL;return err;}
    return cli_wait_connected(s,o->timeout_ms);
}
int cli_open_rpc(const cli_options *o,cli_state *s,viart_rpc **out) {
    s->verbose=o->verbose&&!o->silent;
    viart_rpc_options ro={.endpoint=o->endpoint,.name=o->name,.on_event=cli_rpc_event,
        .user=s,.bearer_token=o->token,.tls_ca_file=o->ca,.call_timeout_ms=o->timeout_ms};
    int err=viart_rpc_create(&ro,out);if(err)return err;
    err=viart_rpc_start(*out);if(err){viart_rpc_destroy(*out);*out=NULL;return err;}
    s->rpc=*out;return cli_wait_connected(s,o->timeout_ms);
}
static struct timespec deadline(unsigned ms) {
    struct timespec t;clock_gettime(CLOCK_REALTIME,&t);
    t.tv_sec+=ms/1000;t.tv_nsec+=(long)(ms%1000)*1000000L;
    if(t.tv_nsec>=1000000000L){t.tv_sec++;t.tv_nsec-=1000000000L;}
    return t;
}
int cli_wait_connected(cli_state *s,unsigned timeout_ms) {
    struct timespec end=deadline(timeout_ms);pthread_mutex_lock(&s->mutex);
    int err=0;while(!s->connected) {
        if(pthread_cond_timedwait(&s->cond,&s->mutex,&end)==ETIMEDOUT){err=ETIMEDOUT;break;}
    }
    pthread_mutex_unlock(&s->mutex);return err;
}
uint64_t cli_count(cli_state *s,int which) {
    if(which==CLI_MESSAGES&&s->fast_count)
        return atomic_load_explicit(&s->fast_messages,memory_order_relaxed);
    pthread_mutex_lock(&s->mutex);uint64_t n=which==CLI_ACKS?s->acks:which==CLI_MESSAGES?s->messages:s->replies;
    pthread_mutex_unlock(&s->mutex);return n;
}
int cli_wait_count(cli_state *s,int which,uint64_t target,unsigned timeout_ms) {
    if(which==CLI_MESSAGES&&s->fast_count){
        uint64_t end=cli_now_ns()+(uint64_t)timeout_ms*1000000ULL;
        struct timespec pause={.tv_nsec=50000};
        while(cli_count(s,which)<target){
            if(cli_now_ns()>=end)return ETIMEDOUT;
            nanosleep(&pause,NULL);
        }
        return 0;
    }
    struct timespec end=deadline(timeout_ms);pthread_mutex_lock(&s->mutex);
    int err=0;for(;;) {
        uint64_t n=which==CLI_ACKS?s->acks:which==CLI_MESSAGES?s->messages:s->replies;
        if(n>=target)break;
        if(s->failed){err=ENOTCONN;break;}
        if(pthread_cond_timedwait(&s->cond,&s->mutex,&end)==ETIMEDOUT){err=ETIMEDOUT;break;}
    }
    pthread_mutex_unlock(&s->mutex);return err;
}
int cli_wait_reply(cli_state *s,uint32_t id,unsigned timeout_ms) {
    struct timespec end=deadline(timeout_ms);pthread_mutex_lock(&s->mutex);
    int err=0;while(s->reply_id!=id) {
        if(s->failed){err=ENOTCONN;break;}
        if(pthread_cond_timedwait(&s->cond,&s->mutex,&end)==ETIMEDOUT){err=ETIMEDOUT;break;}
    }
    if(!err&&s->rpc_error)err=EPROTO;
    pthread_mutex_unlock(&s->mutex);return err;
}
