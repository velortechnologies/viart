#define _POSIX_C_SOURCE 200809L
#include "common.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *cli_read_stdin(size_t *len) {
    size_t cap=4096,n=0;uint8_t *buf=malloc(cap);if(!buf)return NULL;
    for(;;) {
        if(n==cap) {if(cap>=8*1024*1024){free(buf);errno=EMSGSIZE;return NULL;}
            cap*=2;uint8_t *next=realloc(buf,cap);if(!next){free(buf);return NULL;}buf=next;}
        size_t got=fread(buf+n,1,cap-n,stdin);n+=got;
        if(got==0){if(ferror(stdin)){free(buf);return NULL;}break;}
    }
    *len=n;return buf;
}
static int append(bp_pack *p,const uint8_t *data,size_t n){
    if(n>SIZE_MAX-p->len)return EMSGSIZE;
    if(p->len+n>p->cap){size_t cap=p->cap?p->cap:64;
        while(cap<p->len+n){if(cap>SIZE_MAX/2)return EMSGSIZE;cap*=2;}
        uint8_t *next=realloc(p->data,cap);if(!next)return ENOMEM;p->data=next;p->cap=cap;}
    memcpy(p->data+p->len,data,n);p->len+=n;return 0;
}
int cli_pack_scalar(bp_pack *p,const char *value){
    if(!strcmp(value,"true"))return bp_pack_bool(p,true);
    if(!strcmp(value,"false"))return bp_pack_bool(p,false);
    char *end=NULL;errno=0;long long integer=strtoll(value,&end,10);
    if(*value&&end&&!*end&&!errno){
        if(integer>=0)return bp_pack_u64(p,(uint64_t)integer);
        uint8_t out[9]={0xd3};uint64_t x=(uint64_t)integer;
        for(int i=8;i>=1;i--){out[i]=(uint8_t)x;x>>=8;}
        return append(p,out,sizeof(out));
    }
    errno=0;end=NULL;double number=strtod(value,&end);
    if(*value&&end&&!*end&&!errno&&isfinite(number)){
        uint64_t bits;memcpy(&bits,&number,sizeof(bits));uint8_t out[9]={0xcb};
        for(int i=8;i>=1;i--){out[i]=(uint8_t)bits;bits>>=8;}
        return append(p,out,sizeof(out));
    }
    return bp_pack_str(p,value);
}
static void quoted(const uint8_t *p,size_t n) {
    putchar('"');for(size_t i=0;i<n;i++) {
        unsigned x=p[i];if(x=='"'||x=='\\')printf("\\%c",x);
        else if(x=='\n')fputs("\\n",stdout);
        else if(x=='\r')fputs("\\r",stdout);
        else if(x=='\t')fputs("\\t",stdout);
        else if(x<32)printf("\\u%04x",x);
        else putchar((int)x);
    }putchar('"');
}
typedef struct {const uint8_t *p,*end;} unpack;
static bool take(unpack *u,size_t n,const uint8_t **out){if((size_t)(u->end-u->p)<n)return false;*out=u->p;u->p+=n;return true;}
static bool number(unpack *u,size_t n,uint64_t *out){const uint8_t *p;if(!take(u,n,&p))return false;uint64_t x=0;for(size_t i=0;i<n;i++)x=(x<<8)|p[i];*out=x;return true;}
static bool value(unpack *u,unsigned depth) {
    if(depth>16)return false;uint64_t n=0;const uint8_t *p;if(!number(u,1,&n))return false;
    uint8_t tag=(uint8_t)n;
    if(tag<=0x7f){printf("%u",tag);return true;}
    if(tag>=0xe0){printf("%d",(int8_t)tag);return true;}
    if((tag&0xe0)==0xa0){n=tag&31;goto str;}
    if((tag&0xf0)==0x80){n=tag&15;goto map;}
    if((tag&0xf0)==0x90){n=tag&15;goto array;}
    switch(tag) {
    case 0xc0:fputs("null",stdout);return true;
    case 0xc2:fputs("false",stdout);return true;
    case 0xc3:fputs("true",stdout);return true;
    case 0xcb:{if(!number(u,8,&n))return false;double f;memcpy(&f,&n,sizeof(f));
        printf("%.17g",f);return true;}
    case 0xcc:if(!number(u,1,&n))return false;break;
    case 0xcd:if(!number(u,2,&n))return false;break;
    case 0xce:if(!number(u,4,&n))return false;break;
    case 0xcf:if(!number(u,8,&n))return false;break;
    case 0xd0:if(!number(u,1,&n))return false;printf("%d",(int8_t)n);return true;
    case 0xd1:if(!number(u,2,&n))return false;printf("%d",(int16_t)n);return true;
    case 0xd2:if(!number(u,4,&n))return false;printf("%d",(int32_t)n);return true;
    case 0xd3:if(!number(u,8,&n))return false;printf("%lld",(long long)(int64_t)n);return true;
    case 0xd9:if(!number(u,1,&n))return false;goto str;
    case 0xda:if(!number(u,2,&n))return false;goto str;
    case 0xdb:if(!number(u,4,&n))return false;goto str;
    case 0xdc:if(!number(u,2,&n))return false;goto array;
    case 0xdd:if(!number(u,4,&n))return false;goto array;
    case 0xde:if(!number(u,2,&n))return false;goto map;
    case 0xdf:if(!number(u,4,&n))return false;goto map;
    default:return false;
    }
    printf("%llu",(unsigned long long)n);return true;
str:
    if(!take(u,(size_t)n,&p))return false;quoted(p,(size_t)n);return true;
array:
    if(n>100000)return false;putchar('[');for(uint64_t i=0;i<n;i++){
        if(i)putchar(',');if(!value(u,depth+1))return false;
    }putchar(']');return true;
map:
    if(n>100000)return false;putchar('{');for(uint64_t i=0;i<n;i++){
        if(i)putchar(',');if(!value(u,depth+1))return false;
        putchar(':');if(!value(u,depth+1))return false;
    }putchar('}');return true;
}
void cli_print_payload(const uint8_t *data,size_t len,bool silent) {
    if(!len){if(!silent)puts("(empty)");return;}
    bool complex=(data[0]&0xf0)==0x80||data[0]==0xde||data[0]==0xdf||
                 (data[0]&0xf0)==0x90||data[0]==0xdc||data[0]==0xdd;
    if(complex){unpack u={data,data+len};
        if(!silent)fputs("MSGPACK: ",stdout);
        if(value(&u,0)&&u.p==u.end){putchar('\n');return;}
        putchar('\n');
    }
    bool str=true;for(size_t i=0;i<len;i++)if((data[i]<32&&data[i]!='\n'&&data[i]!='\t')||data[i]==127){str=false;break;}
    if(str){fwrite(data,1,len,stdout);if(data[len-1]!='\n')putchar('\n');return;}
    if(silent){fwrite(data,1,len,stdout);return;}
    fputs("HEX: ",stdout);size_t limit=len<256?len:256;
    for(size_t i=0;i<limit;i++)printf("%02x",data[i]);if(limit<len)fputs("...",stdout);putchar('\n');
}
