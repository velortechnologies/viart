#define _POSIX_C_SOURCE 200809L
#include "viart_rpc.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {atomic_int connected, broker_ok, echo_ok, method_error, timeout, notification, failed;} fixture;
static void callback(viart_rpc *rpc,const viart_rpc_event *e,void *user){
 fixture *f=user;
 if(e->kind==VIART_RPC_CONNECTED)atomic_fetch_add(&f->connected,1);
 else if(e->kind==VIART_RPC_REQUEST){
  if(e->method.len==4&&!memcmp(e->method.data,"echo",4)){
   char sender[128];assert(e->sender.len<sizeof(sender));memcpy(sender,e->sender.data,e->sender.len);sender[e->sender.len]=0;
   assert(viart_rpc_reply(rpc,sender,e->id,e->payload.data,e->payload.len,VIART_QOS_PROCESSED)==0);
  }
 }else if(e->kind==VIART_RPC_NOTIFICATION)atomic_fetch_add(&f->notification,1);
 else if(e->kind==VIART_RPC_REPLY){
  if(e->payload.len==5&&!memcmp(e->payload.data,"\x81\xa2ok\xc3",5))atomic_fetch_add(&f->broker_ok,1);
  else if(e->payload.len==4&&!memcmp(e->payload.data,"echo",4))atomic_fetch_add(&f->echo_ok,1);
  else atomic_fetch_add(&f->failed,1);
 }else if(e->kind==VIART_RPC_ERROR){
  if(e->rpc_error_code==-32601)atomic_fetch_add(&f->method_error,1);
  else atomic_fetch_add(&f->failed,1);
 }else if(e->kind==VIART_RPC_TIMEOUT)atomic_fetch_add(&f->timeout,1);
}
static void wait_for(atomic_int *value,int count){
 for(int i=0;i<200&&atomic_load(value)<count;i++){
  struct timespec pause={.tv_nsec=10000000};nanosleep(&pause,NULL);
 }
 assert(atomic_load(value)>=count);
}
int main(void){
 const char *socket_path=getenv("VIART_TEST_SOCKET");if(!socket_path)socket_path="/tmp/viart-test.sock";
 const char *ca=getenv("VIART_TEST_CA"),*token=getenv("VIART_TEST_TOKEN");
 fixture f={0};char a_name[64],b_name[64];
 snprintf(a_name,sizeof(a_name),"rpc.caller.%ld",(long)getpid());
 snprintf(b_name,sizeof(b_name),"rpc.handler.%ld",(long)getpid());
 const char *fixed_a=getenv("VIART_TEST_CALLER"),*fixed_b=getenv("VIART_TEST_HANDLER");
 if(fixed_a){assert(strlen(fixed_a)<sizeof(a_name));strcpy(a_name,fixed_a);}
 if(fixed_b){assert(strlen(fixed_b)<sizeof(b_name));strcpy(b_name,fixed_b);}
 viart_rpc_options ao={.endpoint=socket_path,.name=a_name,.bearer_token=token,.tls_ca_file=ca,
  .on_event=callback,.user=&f,.call_timeout_ms=250};
 viart_rpc_options bo={.endpoint=socket_path,.name=b_name,.bearer_token=token,.tls_ca_file=ca,
  .on_event=callback,.user=&f};
 viart_rpc *a,*b;assert(viart_rpc_create(&ao,&a)==0);assert(viart_rpc_create(&bo,&b)==0);
 assert(viart_rpc_start(a)==0&&viart_rpc_start(b)==0);wait_for(&f.connected,2);
 uint32_t id=0;
 assert(viart_rpc_call(a,".broker","test",NULL,0,VIART_QOS_PROCESSED,&id)==0&&id);
 assert(viart_rpc_call(a,b_name,"echo","echo",4,VIART_QOS_PROCESSED,&id)==0);
 assert(viart_rpc_call(a,".broker","absent.method",NULL,0,VIART_QOS_PROCESSED,&id)==0);
 assert(viart_rpc_notify(a,b_name,"note",4,VIART_QOS_NO)==0);
 assert(viart_rpc_call(a,b_name,"never",NULL,0,VIART_QOS_PROCESSED,&id)==0);
 wait_for(&f.broker_ok,1);wait_for(&f.echo_ok,1);wait_for(&f.method_error,1);
 wait_for(&f.notification,1);wait_for(&f.timeout,1);
 assert(atomic_load(&f.failed)==0);
 viart_rpc_destroy(b);viart_rpc_destroy(a);
 return 0;
}
