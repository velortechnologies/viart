#define _POSIX_C_SOURCE 200809L
#include "acl.h"
#include "subscriptions.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct bp_acl_rule { char *name,*pattern; bp_acl_action action; struct bp_acl_rule *next; };
static int action_parse(const char *text,bp_acl_action *out){
 static const char *names[]={"connect","subscribe","publish","p2p","broadcast"};
 for(int i=0;i<5;i++)if(!strcmp(text,names[i])){*out=(bp_acl_action)i;return 0;}
 return EINVAL;
}
void bp_acl_free(bp_acl_rule *r){while(r){bp_acl_rule *next=r->next;free(r->name);free(r->pattern);free(r);r=next;}}
int bp_acl_load(const char *path,bp_acl_rule **out){
 if(!path||!out)return EINVAL;*out=NULL;FILE *f=fopen(path,"r");if(!f)return errno;
 char *line=NULL;size_t cap=0;ssize_t n;bp_acl_rule *head=NULL;int err=0;
 while((n=getline(&line,&cap,f))>=0){
  (void)n;char *save,*name=strtok_r(line," \t\r\n",&save);if(!name||name[0]=='#')continue;
  char *action=strtok_r(NULL," \t\r\n",&save),*pattern=strtok_r(NULL," \t\r\n",&save);
  char *extra=strtok_r(NULL," \t\r\n",&save);bp_acl_action kind;
  if(!action||!pattern||extra||action_parse(action,&kind)||strchr(name,'*')||strchr(name,'?')){err=EINVAL;break;}
  bp_acl_rule *rule=calloc(1,sizeof(*rule));if(!rule){err=ENOMEM;break;}
  rule->name=strdup(name);rule->pattern=strdup(pattern);rule->action=kind;
  if(!rule->name||!rule->pattern){free(rule->name);free(rule->pattern);free(rule);err=ENOMEM;break;}
  rule->next=head;head=rule;
 }
 if(ferror(f))err=EIO;free(line);fclose(f);
 if(err){bp_acl_free(head);return err;}*out=head;return 0;
}
static bool subnet_match(const char *rule,uint32_t ip){
 char host[64];size_t len=strcspn(rule,"/");if(!len||len>=sizeof(host))return false;
 memcpy(host,rule,len);host[len]=0;struct in_addr addr;
 if(inet_pton(AF_INET,host,&addr)!=1)return false;
 unsigned prefix=32;if(rule[len]=='/'){
  char *end;errno=0;unsigned long n=strtoul(rule+len+1,&end,10);
  if(errno||*end||n>32)return false;prefix=(unsigned)n;
 }
 uint32_t mask=prefix?UINT32_MAX<<(32-prefix):0;
 return (ntohl(addr.s_addr)&mask)==(ip&mask);
}
/* The grant must cover every topic the requested subscription may match. */
static bool topic_covers(const char *grant,const char *request){
 for(;;){
  size_t gn=strcspn(grant,"/"),rn=strcspn(request,"/");
  bool glast=grant[gn]==0,rlast=request[rn]==0;
  if(gn==1&&grant[0]=='#'&&glast)return true;
  if(rn==1&&request[0]=='#'&&rlast)return false;
  if(gn==1&&grant[0]=='+'){}else if(rn==1&&request[0]=='+')return false;
  else if(gn!=rn||memcmp(grant,request,gn))return false;
  if(glast||rlast)return glast&&rlast;
  grant+=gn+1;request+=rn+1;
 }
}
bool bp_acl_allowed(const bp_acl_rule *rules,const char *identity,bp_acl_action action,
                    const char *target,uint32_t ip,bool local,uint32_t uid){
 if(!identity)return false;
 for(const bp_acl_rule *r=rules;r;r=r->next){
  if(r->action!=action||strcmp(r->name,identity))continue;
  if(action==BP_ACL_CONNECT){
   if(local&&!strcmp(r->pattern,"local"))return true;
   if(local&&!strncmp(r->pattern,"uid:",4)){
    char *end;errno=0;unsigned long n=strtoul(r->pattern+4,&end,10);
    if(!errno&&!*end&&n==uid)return true;
   }
   if(!local&&subnet_match(r->pattern,ip))return true;
  }
  else if(!target)continue;
  else if(action==BP_ACL_SUBSCRIBE){if(topic_covers(r->pattern,target))return true;}
  else if(action==BP_ACL_PUBLISH){if(bp_topic_match(r->pattern,target))return true;}
  else if(bp_name_match(r->pattern,target))return true;
 }
 return false;
}
