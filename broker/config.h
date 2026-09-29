#ifndef VIART_BROKER_CONFIG_H
#define VIART_BROKER_CONFIG_H
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    const char *socket_path;
    const char *tcp_bind;
    const char *ws_bind;
    const char *wss_bind,*tls_cert,*tls_key;
    const char *tokens_path;
    const char *acl_path;
    bool allow_public_tcp;
    size_t max_clients, max_frame, max_queue;
    uint64_t handshake_ms, idle_ms;
} bp_config;
int bp_config_parse(int argc, char **argv, bp_config *out);
#endif
