/* libFuzzer target for existing untrusted-input parsers; no protocol changes. */
#include "protocol.h"
#include "rpc_wire.h"
#include "wire.h"
#include "ws.h"
#include "subscriptions.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void within(const uint8_t *base,size_t total,const uint8_t *ptr,size_t len){
 assert(ptr>=base && ptr<=base+total);
 assert(len<=(size_t)(base+total-ptr));
}
int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size){
 bp_command command;size_t used=0;
 if(!bp_parse(data,size,4096,&command,&used)){
  assert(used<=size);within(data,size,command.body,command.body_len);
  if(command.target)within(data,size,command.target,command.target_len);
  if(command.payload)within(data,size,command.payload,command.payload_len);
 }
 viart_wire_in incoming;if(!viart_wire_decode(data,size,&incoming)){
  if(incoming.sender.data)within(data,size,incoming.sender.data,incoming.sender.len);
  if(incoming.topic.data)within(data,size,incoming.topic.data,incoming.topic.len);
  if(incoming.payload.data)within(data,size,incoming.payload.data,incoming.payload.len);
 }
 viart_rpc_frame rpc;if(!viart_rpc_parse(data,size,&rpc)){
  if(rpc.method)within(data,size,rpc.method,rpc.method_len);
  if(rpc.payload)within(data,size,rpc.payload,rpc.payload_len);
 }
 uint8_t op,*payload=NULL;bool fin;size_t payload_len=0,frame_used=0;
 if(!bp_ws_decode(data,size,4096,&frame_used,&op,&fin,&payload,&payload_len)){
  assert(frame_used<=size&&payload_len<=4096);free(payload);
 }
 uint8_t *reply=NULL;size_t reply_len=0;
 if(!bp_ws_upgrade(data,size,&reply,&reply_len)){assert(reply_len>0);free(reply);}
 if(size){size_t left=size/2,right=size-left;
  char *a=malloc(left+1),*b=malloc(right+1);
  if(a&&b){memcpy(a,data,left);a[left]=0;memcpy(b,data+left,right);b[right]=0;
   (void)bp_topic_match(a,b);(void)bp_name_match(a,b);}
  free(a);free(b);
 }
 return 0;
}
