#define _POSIX_C_SOURCE 200809L
#include "common.h"
#include "benchmark.h"
#include "msgpack.h"
#include "rpc_wire.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t running=1;
static void stop_signal(int sig){(void)sig;running=0;}
static void usage(const char *p){
    fprintf(stderr,"usage: %s ENDPOINT [-n NAME] [--token TOKEN] [--ca PEM] [--timeout SEC] [-s] COMMAND\n"
        "  broker {client.list|info|stats|test}\n"
        "  listen [-t TOPIC ...] [--exclude TOPIC ...]\n"
        "  send TARGET [PAYLOAD] | publish TOPIC [PAYLOAD]\n"
        "  rpc {listen [-t TOPIC ...]|notify TARGET [PAYLOAD]|call0 TARGET METHOD [key=value ...|-]|call TARGET METHOD [key=value ...|-]}\n"
        "  benchmark [-w WORKERS] [-i ITERS] [--payload-size BYTES]\n",p);
}
static unsigned parse_uint(const char *s,unsigned max){char *end;errno=0;unsigned long n=strtoul(s,&end,10);if(errno||*end||n>max)return 0;return (unsigned)n;}
static int op_ack(cli_state *s,viart_client *c,const char *target,const void *data,size_t len,
                  bool publish,bool broadcast,unsigned timeout_ms){
    uint64_t before=cli_count(s,CLI_ACKS);int err;
    if(publish)err=viart_client_publish(c,target,data,len,VIART_QOS_PROCESSED,NULL);
    else if(broadcast)err=viart_client_broadcast(c,target,data,len,VIART_QOS_PROCESSED,NULL);
    else err=viart_client_send(c,target,data,len,VIART_QOS_PROCESSED,NULL);
    if(err)return err;err=cli_wait_count(s,CLI_ACKS,before+1,timeout_ms);
    if(!err&&s->ack_result!=1)err=EPROTO;return err;
}
static int rpc_call_wait(cli_state *s,viart_rpc *r,const char *target,const char *method,
                         const void *data,size_t len,unsigned timeout_ms){
    uint32_t id=0;int err=viart_rpc_call(r,target,method,data,len,VIART_QOS_PROCESSED,&id);
    return err?err:cli_wait_reply(s,id,timeout_ms);
}
static int pack_params(int argc,char **argv,bp_pack *p){
    int err=bp_pack_map(p,(size_t)argc);if(err)return err;
    for(int i=0;i<argc;i++){
        char *eq=strchr(argv[i],'=');if(!eq||eq==argv[i])return EINVAL;
        char *key=strndup(argv[i],(size_t)(eq-argv[i]));if(!key)return ENOMEM;
        err=bp_pack_str(p,key);free(key);if(err)return err;
        err=cli_pack_scalar(p,eq+1);if(err)return err;
    }return 0;
}
static int subscribe_all(viart_client *c,cli_state *s,int argc,char **argv,unsigned timeout,bool rpc_mode){
    (void)rpc_mode;
    for(int i=0;i<argc;i++){
        bool exclude=!strcmp(argv[i],"--exclude");
        if(strcmp(argv[i],"-t")&&strcmp(argv[i],"--topics")&&!exclude)return EINVAL;
        if(++i==argc)return EINVAL;
        uint64_t before=cli_count(s,CLI_ACKS);
        int err=exclude?viart_client_exclude(c,argv[i],VIART_QOS_PROCESSED,NULL):
            viart_client_subscribe(c,argv[i],VIART_QOS_PROCESSED,NULL);
        if(err)return err;
        /* RPC wrapper deliberately hides transport ACKs. The registration is
           ordered before later traffic; raw mode verifies the actual ACK. */
        if(!rpc_mode){err=cli_wait_count(s,CLI_ACKS,before+1,timeout);if(err)return err;
            if(s->ack_result!=1)return EPROTO;}
    }return 0;
}
static void listen_until_signal(void){signal(SIGINT,stop_signal);signal(SIGTERM,stop_signal);
    while(running){struct timespec t={.tv_sec=0,.tv_nsec=100000000};nanosleep(&t,NULL);}}
int main(int argc,char **argv){
    if(argc<3){usage(argv[0]);return 2;}
    char name[128];char host[64]="host";if(gethostname(host,sizeof(host)))strcpy(host,"host");
    host[sizeof(host)-1]=0;snprintf(name,sizeof(name),"cli.%s.%ld",host,(long)getpid());
    cli_options o={.endpoint=argv[1],.name=name,.timeout_ms=5000};
    int at=2;while(at<argc){char *a=argv[at];
        if(!strcmp(a,"-n")||!strcmp(a,"--name")){if(++at==argc)goto bad;o.name=argv[at++];}
        else if(!strcmp(a,"--token")){if(++at==argc)goto bad;o.token=argv[at++];}
        else if(!strcmp(a,"--ca")){if(++at==argc)goto bad;o.ca=argv[at++];}
        else if(!strcmp(a,"--timeout")){if(++at==argc)goto bad;
            char *end=NULL;errno=0;double sec=strtod(argv[at++],&end);
            if(errno||!end||*end||sec<0.001||sec>3600)goto bad;
            o.timeout_ms=(unsigned)(sec*1000);if(!o.timeout_ms)o.timeout_ms=1;}
        else if(!strcmp(a,"-s")||!strcmp(a,"--silent")){o.silent=true;at++;}
        else if(!strcmp(a,"-v")||!strcmp(a,"--verbose")){o.verbose=true;at++;}
        else break;
    }
    if(at>=argc)goto bad;
    const char *cmd=argv[at++];
    if(!strcmp(cmd,"benchmark")){
        unsigned workers=1,iters=1000000,payload_size=100;
        while(at<argc){char *a=argv[at++];if(at>=argc)goto bad;
            if(!strcmp(a,"-w")||!strcmp(a,"--workers"))workers=parse_uint(argv[at++],128);
            else if(!strcmp(a,"-i")||!strcmp(a,"--iters"))iters=parse_uint(argv[at++],100000000);
            else if(!strcmp(a,"--payload-size"))payload_size=parse_uint(argv[at++],1024*1024);
            else goto bad;
        }
        if(!workers||!iters||iters<workers||!payload_size)goto bad;
        return cli_benchmark(&o,workers,iters,payload_size)?1:0;
    }
    bool raw_rpc=!strcmp(cmd,"rpc")&&at<argc&&
        (!strcmp(argv[at],"notify")||!strcmp(argv[at],"call0"));
    bool rpc=(!strcmp(cmd,"rpc")&&!raw_rpc)||!strcmp(cmd,"broker");
    cli_state s;cli_state_init(&s);
    s.listen=!strcmp(cmd,"listen");
    s.rpc_listen=!strcmp(cmd,"rpc")&&at<argc&&!strcmp(argv[at],"listen");
    viart_client *c=NULL;viart_rpc *r=NULL;int err=0;
    if(rpc)err=cli_open_rpc(&o,&s,&r);else err=cli_open_raw(&o,&s,&c);
    if(err)goto done;
    if(!strcmp(cmd,"broker")){
        if(at+1!=argc)err=EINVAL;
        else if(strcmp(argv[at],"test")&&strcmp(argv[at],"info")&&
                strcmp(argv[at],"stats")&&strcmp(argv[at],"client.list"))err=EINVAL;
        else {err=rpc_call_wait(&s,r,".broker",argv[at],NULL,0,o.timeout_ms);
            if(!err)cli_print_payload(s.reply_data,s.reply_len,o.silent);}
    } else if(!strcmp(cmd,"send")||!strcmp(cmd,"publish")){
        if(at>=argc||at+2<argc)err=EINVAL;
        else {const char *target=argv[at++];size_t len=0;uint8_t *allocated=NULL;
            const void *data=NULL;if(at<argc){data=argv[at];len=strlen(argv[at]);}
            else {allocated=cli_read_stdin(&len);data=allocated;if(!allocated)err=ENOMEM;}
            if(!err){bool bc=strpbrk(target,"*?")!=NULL;
                err=op_ack(&s,c,target,data,len,!strcmp(cmd,"publish"),bc,o.timeout_ms);}
            free(allocated);if(!err)puts("OK");}
    } else if(!strcmp(cmd,"listen")){
        err=subscribe_all(c,&s,argc-at,argv+at,o.timeout_ms,false);
        if(!err){printf("Listening for %s ...\n",o.name);fflush(stdout);listen_until_signal();}
    } else if(!strcmp(cmd,"rpc")){
        if(at>=argc)err=EINVAL;
        else {const char *sub=argv[at++];
            if(!strcmp(sub,"listen")){
                err=subscribe_all(viart_rpc_transport(r),&s,argc-at,argv+at,o.timeout_ms,true);
                if(!err){printf("Listening for RPC %s ...\n",o.name);fflush(stdout);listen_until_signal();}
            } else if(!strcmp(sub,"notify")){
                if(at>=argc||at+2<argc)err=EINVAL;
                else {const char *target=argv[at++];size_t len=0;uint8_t *allocated=NULL;const void *data=NULL;
                    if(at<argc){data=argv[at];len=strlen(argv[at]);}
                    else {allocated=cli_read_stdin(&len);data=allocated;if(!allocated)err=ENOMEM;}
                    if(!err){uint8_t *encoded=NULL;size_t encoded_len=0;
                        err=viart_rpc_encode_notification(data,len,&encoded,&encoded_len);
                        if(!err)err=op_ack(&s,c,target,encoded,encoded_len,false,false,o.timeout_ms);
                        free(encoded);}
                    free(allocated);if(!err)puts("OK");}
            } else if(!strcmp(sub,"call")||!strcmp(sub,"call0")){
                if(at+2>argc)err=EINVAL;
                else {const char *target=argv[at++],*method=argv[at++];bp_pack p={0};
                    const void *data=NULL;size_t len=0;uint8_t *allocated=NULL;
                    if(at<argc&&argc-at==1&&!strcmp(argv[at],"-")){
                        allocated=cli_read_stdin(&len);data=allocated;if(!allocated)err=ENOMEM;
                    } else if(at<argc){err=pack_params(argc-at,argv+at,&p);data=p.data;len=p.len;}
                    if(!err){if(!strcmp(sub,"call0")){uint8_t *encoded=NULL;size_t encoded_len=0;
                            err=viart_rpc_encode_request(0,method,data,len,&encoded,&encoded_len);
                            if(!err)err=op_ack(&s,c,target,encoded,encoded_len,false,false,o.timeout_ms);
                            free(encoded);if(!err)puts("OK");}
                        else {err=rpc_call_wait(&s,r,target,method,data,len,o.timeout_ms);
                            if(!err)cli_print_payload(s.reply_data,s.reply_len,o.silent);}}
                    free(allocated);bp_pack_free(&p);}
            } else err=EINVAL;
        }
    } else err=EINVAL;
done:
    if(err){fprintf(stderr,"viart-cli: %s",strerror(err));
        if(s.rpc_error)fprintf(stderr,"; RPC code=%d",s.rpc_error);
        fputc('\n',stderr);}
    if(r)viart_rpc_destroy(r);if(c)viart_client_destroy(c);cli_state_fini(&s);
    if(err==EINVAL)usage(argv[0]);return err?1:0;
bad:usage(argv[0]);return 2;
}
