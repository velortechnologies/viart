#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include "transport.h"
#include "wire.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static void emit(viart_client *c, const viart_event *event) {
    if (c->on_event) c->on_event(c, event, c->user);
}
void viart_reactor_wake(viart_client *c) {
    if (c->wakefd >= 0) {
        uint64_t one = 1;
        ssize_t written = write(c->wakefd, &one, sizeof(one));
        (void)written;
    }
}
static void ack(viart_client *c, uint32_t id, uint8_t result) {
    viart_event e = {.kind = VIART_ACK, .id = id, .result = result};
    emit(c, &e);
}
static void fail_pending(viart_client *c, uint8_t code) {
    viart_pending *p = viart_pending_take_all(&c->pending);
    while (p) {
        viart_pending *next = p->next;
        ack(c, p->id, code);
        free(p);
        p = next;
    }
}
static void close_socket(viart_client *c, int err) {
    bool was_ready = atomic_exchange(&c->connected, false);
    if (c->sock >= 0) {
        epoll_ctl(c->epfd, EPOLL_CTL_DEL, c->sock, NULL);
        close(c->sock);
        c->sock = -1;
    }
    c->sock_interest = 0;
    if (c->tx) {
        if (c->tx->qos & 1) ack(c, c->tx->id, 0x73);
        viart_packet_free(c->tx); c->tx = NULL;
    }
    fail_pending(c, 0x73);
    c->rx_len = 0;
    c->phase = VIART_DOWN;
    c->next_connect_ms = now_ms() + c->reconnect_ms;
    if (was_ready || err) {
        viart_event e = {.kind = was_ready ? VIART_DISCONNECTED : VIART_CONNECT_ERROR,
                         .error = err};
        emit(c, &e);
    }
}
static int update_interest(viart_client *c) {
    if (c->sock < 0) return 0;
    bool want_write = c->phase == VIART_CONNECTING || c->tx;
    if (c->phase == VIART_READY) {
        pthread_mutex_lock(&c->queue.mutex);
        want_write |= c->queue.head != NULL;
        pthread_mutex_unlock(&c->queue.mutex);
    }
    struct epoll_event ev = {.events = EPOLLIN | EPOLLRDHUP | (want_write ? EPOLLOUT : 0),
                             .data.fd = c->sock};
    if (ev.events == c->sock_interest) return 0;
    if (epoll_ctl(c->epfd, EPOLL_CTL_MOD, c->sock, &ev) < 0) return errno;
    c->sock_interest = ev.events;
    return 0;
}
static int raw_tx(viart_client *c, const uint8_t *data, size_t len) {
    if (c->tx) return EBUSY;
    c->tx = calloc(1, sizeof(*c->tx));
    if (!c->tx) return ENOMEM;
    c->tx->data = malloc(len);
    if (!c->tx->data) { free(c->tx); c->tx = NULL; return ENOMEM; }
    memcpy(c->tx->data, data, len);
    c->tx->len = len;
    return 0;
}
static int try_connect(viart_client *c) {
    bool in_progress;
    int fd = viart_transport_connect_ex(c->endpoint,c->bearer_token,c->tls_ca_file,&in_progress);
    if (fd < 0) return -fd;
    c->sock = fd;
    c->phase = in_progress ? VIART_CONNECTING : VIART_HANDSHAKE;
    c->handshake = VH_GREETING;
    c->phase_deadline_ms = now_ms() + 5000;
    struct epoll_event ev = {.events = EPOLLIN | EPOLLRDHUP | (in_progress ? EPOLLOUT : 0),
                             .data.fd = fd};
    if (epoll_ctl(c->epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        int err = errno; close(fd); c->sock = -1; c->phase = VIART_DOWN; return err;
    }
    c->sock_interest = ev.events;
    return 0;
}
static void consume(viart_client *c, size_t n) {
    c->rx_len -= n;
    memmove(c->rx, c->rx + n, c->rx_len);
}
static int parse_rx(viart_client *c) {
    for (;;) {
        if (c->phase == VIART_HANDSHAKE) {
            size_t used, response_len;
            uint8_t *response;
            bool ready;
            int err = viart_handshake_step(&c->handshake, c->rx, c->rx_len, c->name,
                                            &used, &response, &response_len, &ready);
            if (err == EAGAIN) return 0;
            if (err) return err;
            consume(c, used);
            c->phase_deadline_ms = now_ms() + 5000;
            if (response) {
                err = raw_tx(c, response, response_len);
                free(response);
                if (err) return err;
            }
            if (ready) {
                c->phase = VIART_READY;
                atomic_store(&c->connected, true);
                c->next_ping_ms = now_ms() + 1000;
                viart_event e = {.kind = VIART_CONNECTED}; emit(c, &e);
            }
        } else if (c->phase == VIART_READY) {
            if (c->rx_len < 6) return 0;
            uint8_t kind = c->rx[0];
            size_t length = (kind == VW_NOP || kind == VW_ACK) ? 0 : viart_le32(c->rx + 1);
            if (length > c->max_frame_bytes) return EMSGSIZE;
            if (c->rx_len < 6 + length) return 0;
            viart_wire_in frame;
            int err = viart_wire_decode(c->rx, 6 + length, &frame);
            if (err) return err;
            if (kind == VW_ACK) {
                if (viart_pending_take(&c->pending, frame.id_or_len)) {
                    ack(c, frame.id_or_len, frame.flag);
                }
            } else if (kind != VW_NOP) {
                viart_event e = {.kind = VIART_MESSAGE, .frame_kind = kind,
                                 .realtime = frame.flag != 0, .sender = frame.sender,
                                 .topic = frame.topic, .payload = frame.payload};
                emit(c, &e);
            }
            consume(c, 6 + length);
        } else return 0;
    }
}
static int read_socket(viart_client *c) {
    for (;;) {
        if (c->rx_cap - c->rx_len < 4096 && c->rx_cap < (size_t)c->max_frame_bytes + 6) {
            size_t max = (size_t)c->max_frame_bytes + 6;
            size_t desired = c->rx_cap ? c->rx_cap * 2 : 8192;
            if (desired > max) desired = max;
            uint8_t *p = realloc(c->rx, desired);
            if (!p) return ENOMEM;
            c->rx = p; c->rx_cap = desired;
        }
        if (c->rx_len == c->rx_cap) return EMSGSIZE;
        ssize_t n = recv(c->sock, c->rx + c->rx_len, c->rx_cap - c->rx_len, 0);
        if (n > 0) {
            c->rx_len += (size_t)n;
            int err = parse_rx(c); if (err) return err;
        } else if (n == 0) return ECONNRESET;
        else if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        else if (errno != EINTR) return errno;
    }
}
static int flush_tx(viart_client *c) {
    if (c->phase != VIART_READY && c->phase != VIART_HANDSHAKE) return 0;
    size_t sent_bytes = 0, sent_frames = 0;
    for (;;) {
        if (!c->tx) {
            if (c->phase != VIART_READY) return 0;
            c->tx = viart_queue_pop(&c->queue);
            if (!c->tx && now_ms() >= c->next_ping_ms) {
                uint8_t ping[9] = {0};
                int err = raw_tx(c, ping, sizeof(ping)); if (err) return err;
                c->next_ping_ms = now_ms() + 1000;
            }
            if (!c->tx) return 0;
        }
        ssize_t n = send(c->sock, c->tx->data + c->tx->offset,
                         c->tx->len - c->tx->offset, MSG_NOSIGNAL);
        if (n > 0) { c->tx->offset += (size_t)n; sent_bytes += (size_t)n; }
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        else if (n < 0 && errno == EINTR) continue;
        else return n == 0 ? EPIPE : errno;
        if (c->tx->offset == c->tx->len) {
            if (c->tx->qos & 1) {
                int err = viart_pending_add(&c->pending, c->tx->id, now_ms() + 5000);
                if (err) return err;
            }
            viart_packet_free(c->tx); c->tx = NULL;
            sent_frames++;
        }
        /* Make room for socket reads even when another thread floods the TX
           queue. Otherwise self-send can fill the broker's outbound queue
           before this reactor reads a single delivered message. */
        if (sent_frames >= 64 || sent_bytes >= 65536) return 0;
    }
}
static void expire_pending(viart_client *c) {
    viart_pending *p = viart_pending_extract_expired(&c->pending, now_ms());
    while (p) {
        viart_pending *next = p->next;
        ack(c, p->id, 0x78);
        free(p);
        p = next;
    }
}

void *viart_reactor_main(void *arg) {
    viart_client *c = arg;
    c->next_connect_ms = 0;
    while (atomic_load(&c->running)) {
        int64_t now = now_ms();
        if (c->on_tick) c->on_tick(c, c->tick_user);
        if (c->phase == VIART_DOWN && now >= c->next_connect_ms) {
            int err = try_connect(c);
            if (err) {
                c->next_connect_ms = now + c->reconnect_ms;
                viart_event e = {.kind = VIART_CONNECT_ERROR, .error = err};
                emit(c, &e);
            }
        }
        if ((c->phase == VIART_CONNECTING || c->phase == VIART_HANDSHAKE) &&
            now >= c->phase_deadline_ms) {
            close_socket(c, ETIMEDOUT);
            continue;
        }
        if (c->phase == VIART_READY) {
            if (now >= c->next_ping_ms && !c->tx) {
                uint8_t ping[9] = {0};
                if (raw_tx(c, ping, sizeof(ping))) { close_socket(c, ENOMEM); continue; }
                c->next_ping_ms = now + 1000;
            }
            expire_pending(c);
        }
        if (c->sock >= 0 && update_interest(c)) { close_socket(c, EIO); continue; }
        struct epoll_event events[8];
        int n = epoll_wait(c->epfd, events, 8, 100);
        if (n < 0) { if (errno == EINTR) continue; break; }
        bool tx_opportunity = false;
        bool tx_flushed = false;
        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == c->wakefd) {
                uint64_t v; while (read(c->wakefd, &v, sizeof(v)) > 0) {}
                tx_opportunity = true;
                continue;
            }
            if (events[i].data.fd != c->sock) continue;
            uint32_t flags = events[i].events;
            int err = 0;
            if (c->phase == VIART_CONNECTING && (flags & (EPOLLOUT | EPOLLERR))) {
                socklen_t len = sizeof(err);
                if (getsockopt(c->sock, SOL_SOCKET, SO_ERROR, &err, &len) < 0) err = errno;
                if (!err) {
                    c->phase = VIART_HANDSHAKE;
                    c->phase_deadline_ms = now_ms() + 5000;
                }
            }
            if (!err && (flags & EPOLLIN)) {
                err = read_socket(c);
                tx_opportunity = true;
            }
            if (!err && (flags & EPOLLOUT) && !tx_flushed) {
                tx_flushed = true;
                err = flush_tx(c);
            }
            if (!err && (flags & (EPOLLHUP | EPOLLRDHUP | EPOLLERR))) err = ECONNRESET;
            if (err) close_socket(c, err);
        }
        /* A wake or read callback may have queued TX after the interest mask
           was set. Flush it in this turn instead of waiting to arm EPOLLOUT
           and take another epoll turn. Never spend the TX budget twice. */
        if (tx_opportunity && !tx_flushed && c->sock >= 0 &&
            (c->phase == VIART_READY || c->phase == VIART_HANDSHAKE)) {
            int err = flush_tx(c);
            if (err) close_socket(c, err);
        }
    }
    close_socket(c, 0);
    free(c->rx); c->rx = NULL; c->rx_cap = c->rx_len = 0;
    return NULL;
}
