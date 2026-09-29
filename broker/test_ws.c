#define _GNU_SOURCE
#include "ws.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
int main(void){
 const char *request="GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
 uint8_t *reply;size_t len;
 assert(!bp_ws_upgrade((const uint8_t *)request,strlen(request),&reply,&len));
 assert(memmem(reply,len,"s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",28));free(reply);
 uint8_t *frame;assert(!bp_ws_encode(2,(const uint8_t *)"abc",3,&frame,&len));
 assert(len==5&&frame[0]==0x82&&frame[1]==3&&!memcmp(frame+2,"abc",3));free(frame);
 const uint8_t masked[]={0x82,0x83,1,2,3,4,'a'^1,'b'^2,'c'^3};
 size_t used,payload_len;uint8_t opcode,*payload;bool fin;
 assert(bp_ws_decode(masked,8,100,&used,&opcode,&fin,&payload,&payload_len)==EAGAIN);
 assert(!bp_ws_decode(masked,sizeof(masked),100,&used,&opcode,&fin,&payload,&payload_len));
 assert(used==sizeof(masked)&&opcode==2&&fin&&payload_len==3&&!memcmp(payload,"abc",3));free(payload);
 assert(bp_ws_decode(masked,sizeof(masked),2,&used,&opcode,&fin,&payload,&payload_len)==EMSGSIZE);
 return 0;
}
