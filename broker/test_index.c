#include "index.h"
#include <assert.h>
static void count(void *owner,void *context){(void)owner;(*(int *)context)++;}
int main(void){
 bp_index idx={0};int a=1,b=2,n=0;
 assert(bp_index_add(&idx,"demo/x",&a)==0);
 assert(bp_index_add(&idx,"demo/#",&b)==0);
 bp_index_visit(&idx,"demo/x",count,&n);assert(n==2);
 bp_index_remove(&idx,"demo/x",&a);n=0;bp_index_visit(&idx,"demo/x",count,&n);assert(n==1);
 bp_index_drop_owner(&idx,&b);n=0;bp_index_visit(&idx,"demo/x",count,&n);assert(n==0);
 bp_index_clear(&idx);return 0;
}
