#define _POSIX_C_SOURCE 200809L
#include "viart.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static atomic_int connected,failed;
static void callback(viart_client *c,const viart_event *e,void *user){
 (void)c;(void)user;if(e->kind==VIART_CONNECTED)atomic_fetch_add(&connected,1);
 else if(e->kind==VIART_CONNECT_ERROR)atomic_fetch_add(&failed,1);
}
int main(int argc,char **argv){
 if(argc!=6){fprintf(stderr,"usage: %s ENDPOINT NAME CA|- TOKEN|- expected_success(0|1)\n",argv[0]);return 2;}
 viart_client_options o={.endpoint=argv[1],.name=argv[2],.on_event=callback,
  .tls_ca_file=argv[3][0]=='-'?NULL:argv[3],.bearer_token=argv[4][0]=='-'?NULL:argv[4],
  .reconnect_ms=100};
 viart_client *c;assert(!viart_client_create(&o,&c));assert(!viart_client_start(c));
 for(int i=0;i<150&&!atomic_load(&connected)&&!atomic_load(&failed);i++){
  struct timespec pause={.tv_nsec=10000000};nanosleep(&pause,NULL);}
 int good=atomic_load(&connected)>0;
 assert(good==atoi(argv[5]));viart_client_destroy(c);return 0;
}
