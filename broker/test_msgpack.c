#include "msgpack.h"
#include <assert.h>
#include <string.h>
int main(void){bp_pack p={0};
 assert(bp_pack_map(&p,1)==0);assert(bp_pack_str(&p,"ok")==0);assert(bp_pack_bool(&p,true)==0);
 assert(p.len==5&&!memcmp(p.data,"\x81\xa2ok\xc3",5));bp_pack_free(&p);
 assert(bp_pack_array(&p,2)==0);assert(bp_pack_u64(&p,256)==0);assert(bp_pack_nil(&p)==0);
 assert(p.len==5&&!memcmp(p.data,"\x92\xcd\x01\x00\xc0",5));bp_pack_free(&p);
 return 0;}
