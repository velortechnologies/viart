#include "rpc.h"
#include "msgpack.h"
#include <errno.h>
#include <regex.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define RPC_METHOD_NOT_FOUND (-32601)
#define RPC_INVALID_PARAMS (-32602)
static int field(bp_pack *p,const char *key,uint64_t value){int e=bp_pack_str(p,key);return e?e:bp_pack_u64(p,value);}
static int string_field(bp_pack *p,const char *key,const char *value){
 int e=bp_pack_str(p,key);if(e)return e;return value?bp_pack_str(p,value):bp_pack_nil(p);
}
static int sorted(const void *a,const void *b){const bp_rpc_client_info *x=a,*y=b;return strcmp(x->name,y->name);}
static int client_info(bp_pack *p,const bp_rpc_client_info *c){
 int e=bp_pack_map(p,10);if(e)return e;
 if((e=string_field(p,"name",c->name))||(e=string_field(p,"kind",c->kind))||
    (e=string_field(p,"source",c->source))||(e=string_field(p,"port",c->port))||
    (e=field(p,"r_frames",c->r_frames))||(e=field(p,"r_bytes",c->r_bytes))||
    (e=field(p,"w_frames",c->w_frames))||(e=field(p,"w_bytes",c->w_bytes))||
    (e=field(p,"queue",c->queue))||(e=field(p,"instances",c->instances)))return e;
 return 0;
}
static int take_str(const uint8_t **cursor,const uint8_t *end,char **out){
 if(*cursor>=end)return EPROTO;uint8_t tag=*(*cursor)++;size_t len;
 if((tag&0xe0)==0xa0)len=tag&31;
 else if(tag==0xd9){if(end-*cursor<1)return EPROTO;len=*(*cursor)++;}
 else if(tag==0xda){if(end-*cursor<2)return EPROTO;len=(size_t)(*cursor)[0]<<8|(*cursor)[1];*cursor+=2;}
 else return EPROTO;
 if((size_t)(end-*cursor)<len)return EPROTO;
 *out=strndup((const char *)*cursor,len);if(!*out)return ENOMEM;*cursor+=len;return 0;
}
static int parse_filter(const uint8_t *data,size_t len,char **filter){
 *filter=NULL;if(!len||(len==1&&data[0]==0xc0))return 0;
 const uint8_t *p=data,*end=data+len;uint8_t tag=*p++;size_t count;
 if((tag&0xf0)==0x80)count=tag&15;
 else if(tag==0xde){if(end-p<2)return EPROTO;count=(size_t)p[0]<<8|p[1];p+=2;}
 else return EPROTO;
 if(count>16)return EPROTO;
 for(size_t i=0;i<count;i++){
  char *key=NULL,*value=NULL;int e=take_str(&p,end,&key);if(e)return e;
  if(!strcmp(key,"filter")&&p<end&&*p==0xc0){p++;}
  else{e=take_str(&p,end,&value);if(e){free(key);return e;}}
  if(!strcmp(key,"filter")){free(*filter);*filter=value;value=NULL;}
  free(key);free(value);
 }
 return p==end?0:EPROTO;
}
static int build_payload(const viart_rpc_frame *r,const bp_rpc_snapshot *s,bp_pack *p,int16_t *code,const char **error){
 if(r->method_len==14&&!memcmp(r->method,"benchmark.test",14)){
  if(r->payload_len){p->data=malloc(r->payload_len);if(!p->data)return ENOMEM;memcpy(p->data,r->payload,r->payload_len);p->len=p->cap=r->payload_len;}
  return 0;
 }
 if(r->method_len==4&&!memcmp(r->method,"test",4)){
  if(r->payload_len){*code=RPC_INVALID_PARAMS;*error="test takes no parameters";return 0;}
  int e=bp_pack_map(p,1);if(e)return e;e=bp_pack_str(p,"ok");return e?e:bp_pack_bool(p,true);
 }
 if(r->method_len==4&&!memcmp(r->method,"info",4)){
  if(r->payload_len){*code=RPC_INVALID_PARAMS;*error="info takes no parameters";return 0;}
  int e=bp_pack_map(p,2);if(e)return e;
  e=string_field(p,"author","VIART C23");return e?e:string_field(p,"version","0.1.0");
 }
 if(r->method_len==5&&!memcmp(r->method,"stats",5)){
  if(r->payload_len){*code=RPC_INVALID_PARAMS;*error="stats takes no parameters";return 0;}
  int e=bp_pack_map(p,5);if(e)return e;
  if((e=field(p,"uptime",s->uptime))||(e=field(p,"r_frames",s->r_frames))||
     (e=field(p,"r_bytes",s->r_bytes))||(e=field(p,"w_frames",s->w_frames))||
     (e=field(p,"w_bytes",s->w_bytes)))return e;
  return 0;
 }
 if(r->method_len==11&&!memcmp(r->method,"client.list",11)){
  char *filter=NULL;int e=parse_filter(r->payload,r->payload_len,&filter);
  if(e){free(filter);*code=RPC_INVALID_PARAMS;*error="invalid client.list parameters";return 0;}
  regex_t re;bool use_re=filter&&*filter;
  if(use_re&&regcomp(&re,filter,REG_EXTENDED|REG_NOSUB)){free(filter);*code=RPC_INVALID_PARAMS;*error="invalid filter regex";return 0;}
  bp_rpc_client_info *copy=malloc(s->clients_len*sizeof(*copy));
  if(!copy&&s->clients_len){if(use_re)regfree(&re);free(filter);return ENOMEM;}
  size_t count=0;for(size_t i=0;i<s->clients_len;i++)
   if(!use_re||regexec(&re,s->clients[i].name,0,NULL,0)==0)copy[count++]=s->clients[i];
  qsort(copy,count,sizeof(*copy),sorted);
  e=bp_pack_map(p,1);if(!e)e=bp_pack_str(p,"clients");if(!e)e=bp_pack_array(p,count);
  for(size_t i=0;i<count&&!e;i++)e=client_info(p,&copy[i]);
  free(copy);if(use_re)regfree(&re);free(filter);return e;
 }
 *code=RPC_METHOD_NOT_FOUND;*error="method not found";return 0;
}
int bp_rpc_serve(const viart_rpc_frame *r,const bp_rpc_snapshot *s,uint8_t **out,size_t *out_len){
 if(!r||!s||!out||!out_len||r->kind!=VR_REQUEST||!r->id)return EINVAL;
 bp_pack payload={0};int16_t code=0;const char *error=NULL;
 int e=build_payload(r,s,&payload,&code,&error);
 if(e){bp_pack_free(&payload);return e;}
 if(code){bp_pack_free(&payload);return viart_rpc_encode_reply(VR_ERROR,r->id,code,error,strlen(error),out,out_len);}
 e=viart_rpc_encode_reply(VR_REPLY,r->id,0,payload.data,payload.len,out,out_len);
 bp_pack_free(&payload);return e;
}
int bp_rpc_event(const char *subject,const char *data,uint64_t time_ns,
                 uint8_t **out,size_t *out_len){
 if(!subject||!out||!out_len)return EINVAL;
 bp_pack p={0};int e=bp_pack_map(&p,data?3:2);
 if(!e)e=string_field(&p,"s",subject);
 if(!e&&data)e=string_field(&p,"d",data);
 if(!e)e=field(&p,"t",time_ns);
 if(e){bp_pack_free(&p);return e;}
 *out=p.data;*out_len=p.len;return 0;
}
