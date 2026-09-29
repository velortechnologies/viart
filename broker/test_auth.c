#define _GNU_SOURCE
#include "auth.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
int main(void){
 char path[]="/tmp/viart-auth-test-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
 FILE *f=fdopen(fd,"w");assert(f);assert(fputs("alice abcdefghijklmnop\nbob 0123456789abcdef\n",f)>=0);assert(!fclose(f));
 assert(!chmod(path,0644));bp_auth_entry *entries=NULL;assert(bp_auth_load(path,&entries)==EACCES);
 assert(!chmod(path,0600));assert(!bp_auth_load(path,&entries));
 uint8_t digest[32];bp_auth_digest("abcdefghijklmnop",16,digest);
 assert(bp_auth_check(entries,"alice",digest));assert(!bp_auth_check(entries,"bob",digest));
 bp_auth_free(entries);unlink(path);return 0;
}
