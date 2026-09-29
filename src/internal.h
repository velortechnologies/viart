#ifndef VIART_INTERNAL_H
#define VIART_INTERNAL_H
#include "queue.h"
#include "handshake.h"
#include "pending.h"
#include "viart.h"
#include <pthread.h>
#include <stdatomic.h>

typedef enum {
    VIART_DOWN, VIART_CONNECTING, VIART_HANDSHAKE, VIART_READY
} viart_phase;

struct viart_client {
    char *endpoint, *name, *bearer_token, *tls_ca_file;
    viart_event_fn on_event;
    void *user;
    void (*on_tick)(viart_client *, void *);
    void *tick_user;
    size_t max_queued_bytes;
    uint32_t max_frame_bytes, reconnect_ms;
    viart_queue queue;
    atomic_uint_fast32_t next_id;
    atomic_bool running, started, connected;
    pthread_t thread;
    int epfd, wakefd, sock;
    uint32_t sock_interest;
    viart_phase phase;
    viart_handshake handshake;
    uint8_t *rx;
    size_t rx_len, rx_cap;
    viart_packet *tx;
    viart_pending *pending;
    int64_t next_connect_ms, next_ping_ms, phase_deadline_ms;
};

void *viart_reactor_main(void *arg);
void viart_reactor_wake(viart_client *client);
#endif
