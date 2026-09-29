#ifndef VIART_BROKER_RPC_H
#define VIART_BROKER_RPC_H
#include "rpc_wire.h"
#include <stddef.h>
#include <stdint.h>
typedef struct {
 const char *name,*kind,*source,*port;
 uint64_t r_frames,r_bytes,w_frames,w_bytes;
 size_t queue,instances;
} bp_rpc_client_info;
typedef struct {
 uint64_t uptime,r_frames,r_bytes,w_frames,w_bytes;
 const bp_rpc_client_info *clients;
 size_t clients_len;
} bp_rpc_snapshot;
/* Produce an RPC payload (not a VIART frame). Caller owns *out. */
int bp_rpc_serve(const viart_rpc_frame *request,const bp_rpc_snapshot *snapshot,
                 uint8_t **out,size_t *out_len);
/* RPC-backed broker announcement payload for .broker/info or .broker/warn. */
int bp_rpc_event(const char *subject,const char *data,uint64_t time_ns,
                 uint8_t **out,size_t *out_len);

#endif
