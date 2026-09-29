#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include "wire.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

int viart_client_create(const viart_client_options *o, viart_client **out) {
    if (!o || !out || !o->endpoint || !o->name || !*o->name) return EINVAL;
    if (strlen(o->name) > UINT16_MAX) return EINVAL;
    viart_client *c = calloc(1, sizeof(*c));
    if (!c) return ENOMEM;
    c->endpoint = strdup(o->endpoint);
    c->name = strdup(o->name);
    c->bearer_token = o->bearer_token ? strdup(o->bearer_token) : NULL;
    c->tls_ca_file = o->tls_ca_file ? strdup(o->tls_ca_file) : NULL;
    if (!c->endpoint || !c->name || (o->bearer_token&&!c->bearer_token) || (o->tls_ca_file&&!c->tls_ca_file)) {
        free(c->endpoint); free(c->name); free(c->bearer_token); free(c->tls_ca_file); free(c); return ENOMEM; }
    c->on_event = o->on_event;
    c->user = o->user;
    c->max_queued_bytes = o->max_queued_bytes ? o->max_queued_bytes : 4u * 1024u * 1024u;
    c->max_frame_bytes = o->max_frame_bytes ? o->max_frame_bytes : 8u * 1024u * 1024u;
    c->reconnect_ms = o->reconnect_ms ? o->reconnect_ms : 1000;
    c->epfd = c->wakefd = c->sock = -1;
    atomic_init(&c->next_id, 0);
    atomic_init(&c->running, false);
    atomic_init(&c->started, false);
    atomic_init(&c->connected, false);
    int err = viart_queue_init(&c->queue, c->max_queued_bytes);
    if (err) { free(c->endpoint); free(c->name); free(c->bearer_token); free(c->tls_ca_file); free(c); return err; }
    *out = c;
    return 0;
}

int viart_client_start(viart_client *c) {
    if (!c) return EINVAL;
    if (atomic_exchange(&c->started, true)) return EALREADY;
    atomic_store(&c->running, true);
    c->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (c->epfd < 0) goto fail;
    c->wakefd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (c->wakefd < 0) goto fail;
    struct epoll_event ev = {.events = EPOLLIN, .data.fd = c->wakefd};
    if (epoll_ctl(c->epfd, EPOLL_CTL_ADD, c->wakefd, &ev) < 0) goto fail;
    int err = pthread_create(&c->thread, NULL, viart_reactor_main, c);
    if (err) { errno = err; goto fail; }
    return 0;
fail:
    {
        int err = errno;
        if (c->wakefd >= 0) close(c->wakefd);
        if (c->epfd >= 0) close(c->epfd);
        c->wakefd = c->epfd = -1;
        atomic_store(&c->running, false);
        atomic_store(&c->started, false);
        return err;
    }
}

void viart_client_stop(viart_client *c) {
    if (!c || !atomic_load(&c->started)) return;
    atomic_store(&c->running, false);
    /* The caller must later join from a different thread before destroy. */
    if (pthread_equal(pthread_self(), c->thread)) { viart_reactor_wake(c); return; }
    if (!atomic_exchange(&c->started, false)) return;
    viart_reactor_wake(c);
    pthread_join(c->thread, NULL);
    close(c->wakefd); close(c->epfd);
    c->wakefd = c->epfd = -1;
}
void viart_client_destroy(viart_client *c) {
    if (!c) return;
    viart_client_stop(c);
    viart_queue_destroy(&c->queue);
    free(c->endpoint); free(c->name); free(c->bearer_token); free(c->tls_ca_file); free(c);
}
bool viart_client_is_connected(const viart_client *c) {
    return c && atomic_load(&c->connected);
}

static int enqueue(viart_client *c, uint8_t op, const char *target,
                   const void *data, size_t len, uint8_t qos, uint32_t *out_id) {
    if (!c || !atomic_load(&c->running)) return ENOTCONN;
    uint32_t id = (uint32_t)atomic_fetch_add(&c->next_id, 1) + 1;
    if (id == 0) id = (uint32_t)atomic_fetch_add(&c->next_id, 1) + 1;
    viart_packet *packet = calloc(1, sizeof(*packet));
    if (!packet) return ENOMEM;
    int err = viart_wire_encode(op, qos, id, target, data, len, &packet->data, &packet->len);
    if (err) { free(packet); return err; }
    packet->id = id; packet->qos = qos;
    bool became_nonempty = false;
    err = viart_queue_push_notify(&c->queue, packet, &became_nonempty);
    if (err) { viart_packet_free(packet); return err; }
    if (out_id) *out_id = id;
    if (became_nonempty) viart_reactor_wake(c);
    return 0;
}
int viart_client_publish(viart_client *c, const char *topic, const void *data,
                         size_t len, uint8_t qos, uint32_t *id) {
    return enqueue(c, VW_PUBLISH, topic, data, len, qos, id);
}
int viart_client_send(viart_client *c, const char *target, const void *data,
                      size_t len, uint8_t qos, uint32_t *id) {
    return enqueue(c, VW_MESSAGE, target, data, len, qos, id);
}
int viart_client_broadcast(viart_client *c, const char *target, const void *data,
                           size_t len, uint8_t qos, uint32_t *id) {
    return enqueue(c, VW_BROADCAST, target, data, len, qos, id);
}
int viart_client_subscribe(viart_client *c, const char *topic, uint8_t qos, uint32_t *id) {
    return enqueue(c, VW_SUBSCRIBE, topic, NULL, 0, qos, id);
}
int viart_client_unsubscribe(viart_client *c, const char *topic, uint8_t qos, uint32_t *id) {
    return enqueue(c, VW_UNSUBSCRIBE, topic, NULL, 0, qos, id);
}

int viart_client_exclude(viart_client *c, const char *topic, uint8_t qos, uint32_t *id) {
    return enqueue(c, VW_EXCLUDE, topic, NULL, 0, qos, id);
}
int viart_client_unexclude(viart_client *c, const char *topic, uint8_t qos, uint32_t *id) {
    return enqueue(c, VW_UNEXCLUDE, topic, NULL, 0, qos, id);
}

int viart_client_publish_for(viart_client *c, const char *topic, const char *receiver,
                             const void *data, size_t len, uint8_t qos, uint32_t *id) {
    if (!receiver || !*receiver || (!data && len)) return EINVAL;
    size_t receiver_len = strlen(receiver);
    if (receiver_len > SIZE_MAX - len - 1) return EMSGSIZE;
    uint8_t *body = malloc(receiver_len + 1 + len);
    if (!body) return ENOMEM;
    memcpy(body, receiver, receiver_len + 1);
    if (len) memcpy(body + receiver_len + 1, data, len);
    int err = enqueue(c, VW_PUBLISH_FOR, topic, body, receiver_len + 1 + len, qos, id);
    free(body);
    return err;
}
