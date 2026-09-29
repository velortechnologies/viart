#define _POSIX_C_SOURCE 200809L
#include "benchmark.h"
#include "rpc_wire.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {pthread_mutex_t mutex;pthread_cond_t cond;bool open,abort;} start_gate;
typedef struct {
    cli_options options;
    cli_state raw_state,rpc_state;
    viart_client *raw;
    viart_rpc *rpc;
    uint8_t *payload;
    size_t payload_size;
    unsigned iterations;
    char raw_name[192],rpc_name[192],null_name[192];
    pthread_barrier_t *barrier;
    start_gate *gate;
    int error;
} worker;

static void tiny_pause(void){struct timespec t={.tv_nsec=1000000};nanosleep(&t,NULL);}
static int queue_send(viart_client *c,const char *target,const void *data,size_t len,uint8_t qos){
    for(unsigned retry=0;retry<10000;retry++){
        int err=viart_client_send(c,target,data,len,qos,NULL);
        if(err!=EAGAIN)return err;tiny_pause();
    }return ETIMEDOUT;
}
static int one_raw(worker *w,const char *target,uint8_t qos,bool receive){
    uint64_t a=qos==VIART_QOS_REALTIME_PROCESSED?cli_count(&w->raw_state,CLI_ACKS):0;
    uint64_t m=receive?cli_count(&w->raw_state,CLI_MESSAGES):0;
    int err=queue_send(w->raw,target,w->payload,w->payload_size,qos);if(err)return err;
    if(qos==VIART_QOS_REALTIME_PROCESSED){err=cli_wait_count(&w->raw_state,CLI_ACKS,a+1,w->options.timeout_ms);
        if(err)return err;
        if(w->raw_state.ack_result!=1&&
           !(strcmp(target,w->null_name)==0&&w->raw_state.ack_result==0x71))return EPROTO;}
    if(receive)return cli_wait_count(&w->raw_state,CLI_MESSAGES,m+1,w->options.timeout_ms);
    return 0;
}
static int one_rpc(worker *w,const char *target,const char *method,bool response){
    if(!response){
        uint8_t *encoded=NULL;size_t len=0;
        int err=viart_rpc_encode_request(0,method,w->payload,w->payload_size,&encoded,&len);
        if(err)return err;
        uint64_t before=cli_count(&w->raw_state,CLI_ACKS);
        err=queue_send(w->raw,target,encoded,len,VIART_QOS_REALTIME_PROCESSED);
        free(encoded);
        if(err)return err;
        err=cli_wait_count(&w->raw_state,CLI_ACKS,before+1,w->options.timeout_ms);
        return err?err:(w->raw_state.ack_result==1||w->raw_state.ack_result==0x71)?0:EPROTO;
    }
    uint32_t id=0;int err=viart_rpc_call(w->rpc,target,method,w->payload,w->payload_size,
                                        VIART_QOS_REALTIME_PROCESSED,&id);
    if(err)return err;err=cli_wait_reply(&w->rpc_state,id,w->options.timeout_ms);
    if(err)return err;
    if(w->rpc_state.reply_len!=w->payload_size||
       memcmp(w->rpc_state.reply_data,w->payload,w->payload_size))return EPROTO;
    return 0;
}
static int run_phase(worker *w,unsigned phase){
    if(phase==2||phase==3){
        /* The sender and receiver run concurrently. The reactor callback is our receiver, so wait for
           the aggregate count only after all sends complete. */
        uint64_t start=cli_count(&w->raw_state,CLI_MESSAGES);
        for(unsigned i=0;i<w->iterations;i++){
            int err=one_raw(w,w->raw_name,
                phase==2?VIART_QOS_NO:VIART_QOS_REALTIME_PROCESSED,false);
            if(err)return err;
        }
        return cli_wait_count(&w->raw_state,CLI_MESSAGES,start+w->iterations,w->options.timeout_ms);
    }
    for(unsigned i=0;i<w->iterations;i++){
        int err=0;
        switch(phase){
        case 0:err=one_raw(w,w->null_name,VIART_QOS_NO,false);break;
        case 1:err=one_raw(w,w->null_name,VIART_QOS_REALTIME_PROCESSED,false);break;
        case 4:err=one_rpc(w,".broker","benchmark.test",true);break;
        case 5:err=one_rpc(w,w->rpc_name,"benchmark.selftest",true);break;
        case 6:err=one_rpc(w,w->null_name,"test",false);break;
        }
        if(err)return err;
    }
    return 0;
}
static void *worker_main(void *arg){
    worker *w=arg;start_gate *g=w->gate;
    pthread_mutex_lock(&g->mutex);while(!g->open)pthread_cond_wait(&g->cond,&g->mutex);
    bool abort=g->abort;pthread_mutex_unlock(&g->mutex);if(abort)return NULL;
    for(unsigned phase=0;phase<7;phase++){
        pthread_barrier_wait(w->barrier);
        if(!w->error)w->error=run_phase(w,phase);
        pthread_barrier_wait(w->barrier);
    }return NULL;
}
static void close_worker(worker *w){
    if(w->rpc)viart_rpc_destroy(w->rpc);if(w->raw)viart_client_destroy(w->raw);
    cli_state_fini(&w->rpc_state);cli_state_fini(&w->raw_state);free(w->payload);
}
int cli_benchmark(const cli_options *opts,unsigned workers,unsigned iters,size_t payload_size){
    static const char *names[]={"send.qos.no","send.qos.processed","send+recv.qos.no",
        "send+recv.qos.processed","rpc.call","rpc.call+handle","rpc.call0"};
    worker *w=calloc(workers,sizeof(*w));pthread_t *threads=calloc(workers,sizeof(*threads));
    if(!w||!threads){free(w);free(threads);return ENOMEM;}
    int err=0;unsigned ready=0,started=0;pthread_barrier_t barrier;
    for(unsigned i=0;i<workers;i++){
        worker *v=&w[i];v->options=*opts;v->iterations=iters/workers+(i<iters%workers);
        v->payload_size=payload_size;v->payload=malloc(payload_size);
        if(!v->payload){err=ENOMEM;break;}memset(v->payload,0xee,payload_size);
        cli_state_init(&v->raw_state);cli_state_init(&v->rpc_state);
        v->raw_state.fast_count=true;ready++;
        snprintf(v->raw_name,sizeof(v->raw_name),"%s-%u",opts->name,i+1);
        snprintf(v->rpc_name,sizeof(v->rpc_name),"%s-%u-rpc",opts->name,i+1);
        snprintf(v->null_name,sizeof(v->null_name),"%s-%u-null",opts->name,i+1);
        v->options.name=v->raw_name;err=cli_open_raw(&v->options,&v->raw_state,&v->raw);if(err)break;
        v->options.name=v->rpc_name;err=cli_open_rpc(&v->options,&v->rpc_state,&v->rpc);if(err)break;
    }
    if(err)goto done;
    if(pthread_barrier_init(&barrier,NULL,workers+1)){err=EINVAL;goto done;}
    start_gate gate={0};pthread_mutex_init(&gate.mutex,NULL);pthread_cond_init(&gate.cond,NULL);
    for(unsigned i=0;i<workers;i++){w[i].barrier=&barrier;w[i].gate=&gate;
        if(pthread_create(&threads[i],NULL,worker_main,&w[i])){err=EAGAIN;break;}
        started++;}
    pthread_mutex_lock(&gate.mutex);gate.abort=err!=0;gate.open=true;
    pthread_cond_broadcast(&gate.cond);pthread_mutex_unlock(&gate.mutex);
    if(err){fprintf(stderr,"benchmark: unable to start workers\n");goto done_barrier;}
    printf("Starting benchmark: %u workers, %u iters, %zu-byte payload\n",workers,iters,payload_size);
    for(unsigned phase=0;phase<7;phase++){
        pthread_barrier_wait(&barrier);uint64_t start=cli_now_ns();
        pthread_barrier_wait(&barrier);uint64_t elapsed=cli_now_ns()-start;
        for(unsigned i=0;i<workers;i++)if(w[i].error&&!err)err=w[i].error;
        printf("%-25s %10.3f ms %12.0f op/s%s\n",names[phase],elapsed/1e6,
               elapsed?(double)iters*1e9/(double)elapsed:0,err?" FAILED":"");
        fflush(stdout);
    }
done_barrier:
    for(unsigned i=0;i<started;i++)pthread_join(threads[i],NULL);
    pthread_cond_destroy(&gate.cond);pthread_mutex_destroy(&gate.mutex);
    pthread_barrier_destroy(&barrier);
done:
    for(unsigned i=0;i<ready;i++)close_worker(&w[i]);
    free(threads);free(w);
    if(err)fprintf(stderr,"benchmark: %s\n",strerror(err));
    return err;
}
