#define _POSIX_C_SOURCE 200809L
#include "acl.h"
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void){
 char path[]="/tmp/viart-acl-test-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
 FILE *f=fdopen(fd,"w");assert(f);
 assert(fputs("alice connect 127.0.0.0/8\nalice connect uid:1000\nalice subscribe telemetry/#\nalice publish telemetry/+\nalice p2p worker.*\n",f)>=0);
 assert(!fclose(f));bp_acl_rule *rules=NULL;assert(!bp_acl_load(path,&rules));unlink(path);
 assert(bp_acl_allowed(rules,"alice",BP_ACL_CONNECT,NULL,0x7f000001,false,0));
 assert(bp_acl_allowed(rules,"alice",BP_ACL_CONNECT,NULL,0,true,1000));
 assert(!bp_acl_allowed(rules,"alice",BP_ACL_CONNECT,NULL,0,true,1001));
 assert(!bp_acl_allowed(rules,"alice",BP_ACL_CONNECT,NULL,0x0a000001,false,0));
 assert(!bp_acl_allowed(rules,"bob",BP_ACL_CONNECT,NULL,0x7f000001,false,0));
 assert(bp_acl_allowed(rules,"alice",BP_ACL_SUBSCRIBE,"telemetry/+",0,false,0));
 assert(!bp_acl_allowed(rules,"alice",BP_ACL_SUBSCRIBE,"#",0,false,0));
 assert(bp_acl_allowed(rules,"alice",BP_ACL_PUBLISH,"telemetry/temp",0,false,0));
 assert(!bp_acl_allowed(rules,"alice",BP_ACL_PUBLISH,"secret/temp",0,false,0));
 assert(bp_acl_allowed(rules,"alice",BP_ACL_P2P,"worker.one",0,false,0));
 assert(!bp_acl_allowed(rules,"alice",BP_ACL_P2P,"other",0,false,0));
 bp_acl_free(rules);return 0;
}
