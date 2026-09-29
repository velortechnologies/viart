#ifndef VIART_BROKER_MSGPACK_H
#define VIART_BROKER_MSGPACK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { uint8_t *data; size_t len,cap; } bp_pack;
void bp_pack_free(bp_pack *p);
int bp_pack_map(bp_pack *p,size_t count);
int bp_pack_array(bp_pack *p,size_t count);
int bp_pack_str(bp_pack *p,const char *s);
int bp_pack_u64(bp_pack *p,uint64_t n);
int bp_pack_bool(bp_pack *p,bool value);
int bp_pack_nil(bp_pack *p);
#endif
