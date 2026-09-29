#ifndef VIART_RPC_H
#define VIART_RPC_H
#include "viart.h"
#include <stddef.h>
#include <stdint.h>

typedef struct viart_rpc viart_rpc;
typedef enum {
    VIART_RPC_CONNECTED = 1, VIART_RPC_DISCONNECTED,
    VIART_RPC_REQUEST, VIART_RPC_NOTIFICATION,
    VIART_RPC_REPLY, VIART_RPC_ERROR, VIART_RPC_TIMEOUT,
    VIART_RPC_FRAME
} viart_rpc_event_kind;
typedef struct {
    viart_rpc_event_kind kind;
    uint32_t id;
    int error;                  /* errno for disconnect, failed call, or timeout */
    int16_t rpc_error_code;     /* only for VIART_RPC_ERROR */
    viart_bytes sender, method, payload;
    const viart_event *frame;   /* only for non-RPC VIART_RPC_FRAME */
} viart_rpc_event;
typedef void (*viart_rpc_event_fn)(viart_rpc *, const viart_rpc_event *, void *user);
typedef struct {
    const char *endpoint, *name;
    viart_rpc_event_fn on_event;
    void *user;
    uint32_t call_timeout_ms;  /* 0 = 5000 ms */
    uint32_t reconnect_ms;     /* 0 = 1000 ms */
    size_t max_queued_bytes;   /* 0 = 4 MiB */
    uint32_t max_frame_bytes;  /* 0 = 8 MiB */
    const char *bearer_token, *tls_ca_file;
} viart_rpc_options;
int viart_rpc_create(const viart_rpc_options *, viart_rpc **out);
int viart_rpc_start(viart_rpc *);
void viart_rpc_stop(viart_rpc *);
void viart_rpc_destroy(viart_rpc *);
viart_client *viart_rpc_transport(viart_rpc *); /* borrowed: pub/sub remains available */
int viart_rpc_call(viart_rpc *, const char *target, const char *method,
                   const void *params, size_t len, uint8_t qos, uint32_t *call_id);
int viart_rpc_call0(viart_rpc *, const char *target, const char *method,
                    const void *params, size_t len, uint8_t qos);
int viart_rpc_notify(viart_rpc *, const char *target,
                     const void *data, size_t len, uint8_t qos);
int viart_rpc_reply(viart_rpc *, const char *target, uint32_t call_id,
                    const void *data, size_t len, uint8_t qos);
int viart_rpc_error(viart_rpc *, const char *target, uint32_t call_id,
                    int16_t code, const void *data, size_t len, uint8_t qos);
#endif
