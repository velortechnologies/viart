#ifndef VIART_CLI_COMMON_H
#define VIART_CLI_COMMON_H
#include "viart.h"
#include "viart_rpc.h"
#include "msgpack.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool connected, listen, rpc_listen, failed, verbose;
    bool fast_count;
    atomic_uint_fast64_t fast_messages;
    uint64_t acks, messages, replies;
    uint8_t ack_result;
    uint32_t reply_id;
    int16_t rpc_error;
    uint8_t *reply_data;
    size_t reply_len;
    viart_rpc *rpc;
} cli_state;

typedef struct {
    const char *endpoint, *name, *token, *ca;
    unsigned timeout_ms;
    bool silent, verbose;
} cli_options;

void cli_state_init(cli_state *s);
void cli_state_fini(cli_state *s);
void cli_raw_event(viart_client *c, const viart_event *e, void *user);
void cli_rpc_event(viart_rpc *r, const viart_rpc_event *e, void *user);
int cli_open_raw(const cli_options *o, cli_state *s, viart_client **out);
int cli_open_rpc(const cli_options *o, cli_state *s, viart_rpc **out);
int cli_wait_connected(cli_state *s, unsigned timeout_ms);
int cli_wait_count(cli_state *s, int which, uint64_t target, unsigned timeout_ms);
int cli_wait_reply(cli_state *s, uint32_t id, unsigned timeout_ms);
uint64_t cli_count(cli_state *s, int which);
void cli_print_payload(const uint8_t *data, size_t len, bool silent);
uint8_t *cli_read_stdin(size_t *len);
int cli_pack_scalar(bp_pack *p,const char *value);
uint64_t cli_now_ns(void);

enum { CLI_ACKS=1, CLI_MESSAGES=2, CLI_REPLIES=3 };
#endif
