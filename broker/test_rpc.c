#include "rpc.h"
#include "rpc_wire.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
int main(void){
 bp_rpc_snapshot s={.uptime=5};uint8_t *request,*reply;size_t len,reply_len;
 assert(viart_rpc_encode_request(7,"test",NULL,0,&request,&len)==0);
 viart_rpc_frame parsed;assert(viart_rpc_parse(request,len,&parsed)==0);
 assert(bp_rpc_serve(&parsed,&s,&reply,&reply_len)==0);free(request);
 assert(viart_rpc_parse(reply,reply_len,&parsed)==0);
 assert(parsed.kind==VR_REPLY&&parsed.id==7&&parsed.payload_len==5);
 assert(!memcmp(parsed.payload,"\x81\xa2ok\xc3",5));free(reply);
 return 0;
}
