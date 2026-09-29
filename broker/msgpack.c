#include "msgpack.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
static int bytes(bp_pack *p,const void *src,size_t n){
 if(!p||(!src&&n)||n>SIZE_MAX-p->len)return EINVAL;
 if(p->len+n>p->cap){size_t cap=p->cap?p->cap:64;while(cap<p->len+n){if(cap>SIZE_MAX/2)return EMSGSIZE;cap*=2;}
  uint8_t *new_data=realloc(p->data,cap);if(!new_data)return ENOMEM;p->data=new_data;p->cap=cap;}
 if(n)memcpy(p->data+p->len,src,n);p->len+=n;return 0;
}
static int byte(bp_pack *p,uint8_t x){return bytes(p,&x,1);}
static int header(bp_pack *p,size_t n,uint8_t tiny,uint8_t big16,uint8_t big32){
 if(n<16)return byte(p,tiny|(uint8_t)n);
 if(n<=UINT16_MAX){uint8_t x[]={big16,(uint8_t)(n>>8),(uint8_t)n};return bytes(p,x,3);}
 if(n>UINT32_MAX)return EMSGSIZE;
 uint8_t x[]={big32,(uint8_t)(n>>24),(uint8_t)(n>>16),(uint8_t)(n>>8),(uint8_t)n};return bytes(p,x,5);
}
void bp_pack_free(bp_pack *p){if(p){free(p->data);*p=(bp_pack){0};}}
int bp_pack_map(bp_pack *p,size_t n){return header(p,n,0x80,0xde,0xdf);}
int bp_pack_array(bp_pack *p,size_t n){return header(p,n,0x90,0xdc,0xdd);}
int bp_pack_str(bp_pack *p,const char *s){
 if(!s)return EINVAL;size_t n=strlen(s);int err;
 if(n<32)err=byte(p,0xa0|(uint8_t)n);
 else if(n<=UINT8_MAX){uint8_t x[]={0xd9,(uint8_t)n};err=bytes(p,x,2);}
 else if(n<=UINT16_MAX){uint8_t x[]={0xda,(uint8_t)(n>>8),(uint8_t)n};err=bytes(p,x,3);}
 else return EMSGSIZE;
 return err?err:bytes(p,s,n);
}
int bp_pack_u64(bp_pack *p,uint64_t n){
 if(n<=127)return byte(p,(uint8_t)n);
 if(n<=UINT8_MAX){uint8_t x[]={0xcc,(uint8_t)n};return bytes(p,x,2);}
 if(n<=UINT16_MAX){uint8_t x[]={0xcd,(uint8_t)(n>>8),(uint8_t)n};return bytes(p,x,3);}
 if(n<=UINT32_MAX){uint8_t x[]={0xce,(uint8_t)(n>>24),(uint8_t)(n>>16),(uint8_t)(n>>8),(uint8_t)n};return bytes(p,x,5);}
 uint8_t x[9]={0xcf};for(int i=8;i>=1;i--){x[i]=(uint8_t)n;n>>=8;}return bytes(p,x,9);
}
int bp_pack_bool(bp_pack *p,bool v){return byte(p,v?0xc3:0xc2);}
int bp_pack_nil(bp_pack *p){return byte(p,0xc0);}
