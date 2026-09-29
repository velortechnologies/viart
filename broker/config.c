#include "config.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int number(const char *text, uint64_t min, uint64_t max, uint64_t *out) {
    if (!text || !*text || *text == '-') return EINVAL;
    char *end;
    errno = 0;
    unsigned long long n = strtoull(text, &end, 10);
    if (errno || *end || n < min || n > max) return EINVAL;
    *out = (uint64_t)n;
    return 0;
}
int bp_config_parse(int argc, char **argv, bp_config *out) {
    if (!out) return EINVAL;
    *out = (bp_config){.max_clients=1024, .max_frame=8u*1024u*1024u,
                       .max_queue=4u*1024u*1024u, .handshake_ms=5000, .idle_ms=30000};
    for (int i=1; i<argc; i++) {
        if (!strcmp(argv[i], "-B") && i+1<argc) out->socket_path=argv[++i];
        else if (!strcmp(argv[i], "--tcp") && i+1<argc) out->tcp_bind=argv[++i];
        else if (!strcmp(argv[i], "--ws") && i+1<argc) out->ws_bind=argv[++i];
        else if (!strcmp(argv[i], "--wss") && i+1<argc) out->wss_bind=argv[++i];
        else if (!strcmp(argv[i], "--tls-cert") && i+1<argc) out->tls_cert=argv[++i];
        else if (!strcmp(argv[i], "--tls-key") && i+1<argc) out->tls_key=argv[++i];
        else if (!strcmp(argv[i], "--tokens") && i+1<argc) out->tokens_path=argv[++i];
        else if (!strcmp(argv[i], "--acl") && i+1<argc) out->acl_path=argv[++i];
        else if (!strcmp(argv[i], "--allow-public-tcp")) out->allow_public_tcp=true;
        else {
            if (i+1>=argc) return EINVAL;
            uint64_t value;
            const char *key=argv[i], *arg=argv[++i];
            if (!strcmp(key,"--max-clients")) {
                if (number(arg,1,100000,&value)) return EINVAL;
                out->max_clients=(size_t)value;
            } else if (!strcmp(key,"--max-frame")) {
                if (number(arg,1,64u*1024u*1024u,&value)) return EINVAL;
                out->max_frame=(size_t)value;
            } else if (!strcmp(key,"--max-queue")) {
                if (number(arg,6,64u*1024u*1024u,&value)) return EINVAL;
                out->max_queue=(size_t)value;
            } else if (!strcmp(key,"--handshake-ms")) {
                if (number(arg,10,600000,&value)) return EINVAL;
                out->handshake_ms=value;
            } else if (!strcmp(key,"--idle-ms")) {
                if (number(arg,100,86400000,&value)) return EINVAL;
                out->idle_ms=value;
            } else return EINVAL;
        }
    }
    if ((out->wss_bind && (!out->tls_cert || !out->tls_key)) ||
        ((out->tls_cert || out->tls_key || out->tokens_path) && !out->wss_bind)) return EINVAL;
    return (out->socket_path && *out->socket_path) || (out->tcp_bind && *out->tcp_bind) ||
           (out->ws_bind && *out->ws_bind) || (out->wss_bind && *out->wss_bind) ? 0 : EINVAL;
}
