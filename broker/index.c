#define _POSIX_C_SOURCE 200809L
#include "index.h"
#include "subscriptions.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static unsigned bucket(const char *s){uint32_t hash=2166136261u;for(;*s;s++)hash=(hash^(unsigned char)*s)*16777619u;return hash&255u;}
static bool wildcard(const char *s){return strchr(s,'+')||strchr(s,'#');}
int bp_index_add(bp_index *idx,const char *pattern,void *owner){
 if(!idx||!pattern||!owner)return EINVAL;
 bp_index_entry **head=wildcard(pattern)?&idx->wildcards:&idx->exact[bucket(pattern)];
 for(bp_index_entry *e=*head;e;e=e->next)if(e->owner==owner&&!strcmp(e->pattern,pattern))return 0;
 bp_index_entry *e=malloc(sizeof(*e));if(!e)return ENOMEM;
 e->pattern=strdup(pattern);if(!e->pattern){free(e);return ENOMEM;}e->owner=owner;e->next=*head;*head=e;return 0;
}
static void remove_from(bp_index_entry **head,const char *pattern,void *owner){
 while(*head){bp_index_entry *e=*head;if((!owner||e->owner==owner)&&(!pattern||!strcmp(e->pattern,pattern))){*head=e->next;free(e->pattern);free(e);}
 else head=&e->next;}
}
void bp_index_remove(bp_index *idx,const char *pattern,void *owner){
 if(!idx||!pattern)return;remove_from(wildcard(pattern)?&idx->wildcards:&idx->exact[bucket(pattern)],pattern,owner);
}
void bp_index_drop_owner(bp_index *idx,void *owner){
 if(!idx)return;remove_from(&idx->wildcards,NULL,owner);for(size_t i=0;i<256;i++)remove_from(&idx->exact[i],NULL,owner);
}
void bp_index_visit(const bp_index *idx,const char *topic,bp_index_visit_fn fn,void *context){
 if(!idx||!topic||!fn)return;
 for(bp_index_entry *e=idx->exact[bucket(topic)];e;e=e->next)if(!strcmp(e->pattern,topic))fn(e->owner,context);
 for(bp_index_entry *e=idx->wildcards;e;e=e->next)if(bp_topic_match(e->pattern,topic))fn(e->owner,context);
}
void bp_index_clear(bp_index *idx){if(!idx)return;remove_from(&idx->wildcards,NULL,NULL);for(size_t i=0;i<256;i++)remove_from(&idx->exact[i],NULL,NULL);}
