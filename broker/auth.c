#define _GNU_SOURCE
#include "auth.h"
#include <openssl/crypto.h>
#include <openssl/sha.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct bp_auth_entry{char *name;uint8_t digest[32];struct bp_auth_entry *next;};
void bp_auth_digest(const char *token,size_t len,uint8_t digest[32]){SHA256((const unsigned char *)token,len,digest);}
void bp_auth_free(bp_auth_entry *e){while(e){bp_auth_entry *next=e->next;free(e->name);OPENSSL_cleanse(e->digest,32);free(e);e=next;}}
int bp_auth_load(const char *path,bp_auth_entry **out){
 if(!path||!out)return EINVAL;*out=NULL;FILE *f=fopen(path,"r");if(!f)return errno;
 struct stat st;if(fstat(fileno(f),&st)<0){int err=errno;fclose(f);return err;}
 if(!S_ISREG(st.st_mode)||(st.st_mode&077)){fclose(f);return EACCES;}
 bp_auth_entry *head=NULL;char *line=NULL;size_t cap=0;int err=0;
 while(getline(&line,&cap,f)>=0){char *save,*name=strtok_r(line," \t\r\n",&save);
  if(!name||name[0]=='#')continue;char *token=strtok_r(NULL," \t\r\n",&save);
  if(!token||strtok_r(NULL," \t\r\n",&save)||strlen(token)<16||strlen(token)>256||
     strchr(name,'%')||strchr(name,'*')){err=EINVAL;break;}
  for(bp_auth_entry *e=head;e;e=e->next)if(!strcmp(e->name,name)){err=EINVAL;break;}
  if(err)break;bp_auth_entry *e=calloc(1,sizeof(*e));if(!e){err=ENOMEM;break;}
  e->name=strdup(name);if(!e->name){free(e);err=ENOMEM;break;}
  bp_auth_digest(token,strlen(token),e->digest);e->next=head;head=e;
 }
 if(ferror(f))err=EIO;if(line){OPENSSL_cleanse(line,cap);free(line);}fclose(f);
 if(err||!head){bp_auth_free(head);return err?err:EINVAL;}*out=head;return 0;
}
bool bp_auth_check(const bp_auth_entry *entries,const char *name,const uint8_t digest[32]){
 for(const bp_auth_entry *e=entries;e;e=e->next)
  if(!strcmp(e->name,name)&&CRYPTO_memcmp(e->digest,digest,32)==0)return true;
 return false;
}
