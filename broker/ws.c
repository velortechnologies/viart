#define _GNU_SOURCE
#include "ws.h"
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *header_value(const char *h,const char *name){
 size_t n=strlen(name);const char *p=strstr(h,"\r\n");if(!p)return NULL;
 while((p=strstr(p,"\r\n"))){p+=2;if(!*p||!strncmp(p,"\r\n",2))break;
  if(!strncasecmp(p,name,n)&&p[n]==':'){p+=n+1;while(*p==' '||*p=='\t')p++;return p;}
 }return NULL;
}
static bool line_has_token(const char *line,const char *token){
 size_t len=strcspn(line,"\r\n"),n=strlen(token);
 for(size_t i=0;i+n<=len;i++)if(!strncasecmp(line+i,token,n)&&
    (i==0||line[i-1]==' '||line[i-1]=='\t'||line[i-1]==',')&&
    (i+n==len||line[i+n]==' '||line[i+n]=='\t'||line[i+n]==','))return true;
 return false;
}
int bp_ws_bearer(const uint8_t *header,size_t len,char **token){
 if(!header||!token||len>8192)return EINVAL;*token=NULL;
 char *copy=malloc(len+1);if(!copy)return ENOMEM;memcpy(copy,header,len);copy[len]=0;
 const char *value=header_value(copy,"Authorization");int err=EACCES;
 if(value&&!strncasecmp(value,"Bearer ",7)){
  value+=7;size_t n=strcspn(value,"\r\n");
  if(n>=16&&n<=256&&memchr(value,' ',n)==NULL){*token=strndup(value,n);err=*token?0:ENOMEM;}
 }
 free(copy);return err;
}
int bp_ws_upgrade(const uint8_t *header,size_t len,uint8_t **reply,size_t *reply_len){
 if(!header||!reply||!reply_len||len>8192)return EINVAL;
 char *copy=malloc(len+1);if(!copy)return ENOMEM;memcpy(copy,header,len);copy[len]=0;
 if(strncmp(copy,"GET ",4)||!strstr(copy,"\r\n\r\n")){free(copy);return EPROTO;}
 const char *key=header_value(copy,"Sec-WebSocket-Key"),*version=header_value(copy,"Sec-WebSocket-Version");
 const char *upgrade=header_value(copy,"Upgrade"),*connection=header_value(copy,"Connection");
 if(!key||!version||!upgrade||!connection||strcspn(upgrade,"\r\n")!=9||
    strncasecmp(upgrade,"websocket",9)||strcspn(version,"\r\n")!=2||
    strncmp(version,"13",2)||!line_has_token(connection,"upgrade")){free(copy);return EPROTO;}
 size_t key_len=strcspn(key,"\r\n");
 unsigned char nonce[18];if(key_len!=24||key[22]!='='||key[23]!='='||
    EVP_DecodeBlock(nonce,(const unsigned char *)key,24)!=18){free(copy);return EPROTO;}
 char joined[61];memcpy(joined,key,24);memcpy(joined+24,"258EAFA5-E914-47DA-95CA-C5AB0DC85B11",36);
 unsigned char digest[SHA_DIGEST_LENGTH];SHA1((unsigned char *)joined,60,digest);
 unsigned char accept[29];EVP_EncodeBlock(accept,digest,SHA_DIGEST_LENGTH);
 static const char prefix[]="HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ";
 size_t n=sizeof(prefix)-1+28+4;uint8_t *response=malloc(n);if(!response){free(copy);return ENOMEM;}
 memcpy(response,prefix,sizeof(prefix)-1);memcpy(response+sizeof(prefix)-1,accept,28);
 memcpy(response+sizeof(prefix)-1+28,"\r\n\r\n",4);
 *reply=response;*reply_len=n;free(copy);return 0;
}
int bp_ws_encode(uint8_t opcode,const uint8_t *payload,size_t len,uint8_t **out,size_t *out_len){
 if(!out||!out_len||(!payload&&len))return EINVAL;
 size_t hdr=len<126?2:len<=65535?4:10;if(len>SIZE_MAX-hdr)return EOVERFLOW;
 uint8_t *buf=malloc(hdr+len);if(!buf)return ENOMEM;
 buf[0]=0x80|(opcode&0x0f);buf[1]=len<126?(uint8_t)len:len<=65535?126:127;
 if(hdr==4){buf[2]=(uint8_t)(len>>8);buf[3]=(uint8_t)len;}
 else if(hdr==10)for(int i=0;i<8;i++)buf[2+i]=(uint8_t)((uint64_t)len>>(56-8*i));
 if(len)memcpy(buf+hdr,payload,len);*out=buf;*out_len=hdr+len;return 0;
}
int bp_ws_decode(const uint8_t *data,size_t len,size_t max_payload,size_t *used,
                 uint8_t *opcode,bool *fin,uint8_t **payload,size_t *payload_len){
 if(!data||!used||!opcode||!fin||!payload||!payload_len)return EINVAL;
 if(len<2)return EAGAIN;*fin=(data[0]&0x80)!=0;*opcode=data[0]&0x0f;
 if((data[0]&0x70)||!(data[1]&0x80))return EPROTO;
 size_t hdr=2, n=data[1]&0x7f;
 if(n==126){if(len<4)return EAGAIN;n=((size_t)data[2]<<8)|data[3];hdr=4;}
 else if(n==127){if(len<10)return EAGAIN;uint64_t wide=0;for(int i=0;i<8;i++)wide=(wide<<8)|data[2+i];
  if(wide>SIZE_MAX)return EMSGSIZE;n=(size_t)wide;hdr=10;}
 if(n>max_payload||n>SIZE_MAX-hdr-4)return EMSGSIZE;
 if(len<hdr+4+n)return EAGAIN;
 if(*opcode>=8&&(!*fin||n>125))return EPROTO;
 const uint8_t *mask=data+hdr;const uint8_t *src=mask+4;uint8_t *decoded=malloc(n?n:1);if(!decoded)return ENOMEM;
 for(size_t i=0;i<n;i++)decoded[i]=src[i]^mask[i&3];
 *used=hdr+4+n;*payload=decoded;*payload_len=n;return 0;
}
