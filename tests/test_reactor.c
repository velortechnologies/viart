#define _POSIX_C_SOURCE 200809L
#include "viart.h"
#include "wire.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

typedef struct { int server; pthread_mutex_t lock; int connected, ack, message; } fixture;
static atomic_int connect_errors;
static void error_callback(viart_client *c, const viart_event *e, void *arg) {
    (void)c; (void)arg;
    if (e->kind == VIART_CONNECT_ERROR && e->error != 0) atomic_fetch_add(&connect_errors, 1);
}
static void exact(int fd, void *buf, size_t len) {
    size_t pos = 0;
    while (pos < len) { ssize_t n = recv(fd, (char *)buf + pos, len - pos, 0); assert(n > 0); pos += (size_t)n; }
}
static void callback(viart_client *c, const viart_event *e, void *arg) {
    fixture *f = arg;
    pthread_mutex_lock(&f->lock);
    if (e->kind == VIART_CONNECTED) {
        f->connected++;
        assert(viart_client_subscribe(c, "unit/test", VIART_QOS_PROCESSED, NULL) == 0);
    } else if (e->kind == VIART_ACK) {
        assert(e->result == 1); f->ack++;
    } else if (e->kind == VIART_MESSAGE) {
        assert(e->topic.len == 9 && memcmp(e->topic.data, "unit/test", 9) == 0);
        assert(e->payload.len == 4 && memcmp(e->payload.data, "data", 4) == 0);
        f->message++;
    }
    pthread_mutex_unlock(&f->lock);
}
static void *server(void *arg) {
    fixture *f = arg;
    for (int cycle = 0; cycle < 2; cycle++) {
    int fd = accept(f->server, NULL, NULL); assert(fd >= 0);
    uint8_t hello[3] = {0xEB, 1, 0}, buf[64];
    assert(send(fd, hello, 1, 0) == 1);
    struct timespec pause = {.tv_nsec = 10000000}; nanosleep(&pause, NULL);
    assert(send(fd, hello + 1, 2, 0) == 2);
    exact(fd, buf, 3); assert(memcmp(buf, hello, 3) == 0);
    uint8_t ok = 1; assert(send(fd, &ok, 1, 0) == 1);
    exact(fd, buf, 2); uint16_t name_len = viart_le16(buf);
    assert(name_len == strlen("viart-reactor")); exact(fd, buf, name_len);
    assert(memcmp(buf, "viart-reactor", name_len) == 0);
    assert(send(fd, &ok, 1, 0) == 1);
    exact(fd, buf, 9); assert((buf[4] & 0x3f) == VW_SUBSCRIBE);
    uint32_t id = viart_le32(buf), n = viart_le32(buf + 5);
    assert(n == 9); exact(fd, buf, n); assert(memcmp(buf, "unit/test", n) == 0);
    uint8_t ack[6] = {VW_ACK}; viart_store32(ack + 1, id); ack[5] = 1;
    assert(send(fd, ack, sizeof(ack), 0) == (ssize_t)sizeof(ack));
    uint8_t msg[6 + 6 + 1 + 9 + 1 + 4] = {VW_PUBLISH};
    viart_store32(msg + 1, sizeof(msg) - 6);
    memcpy(msg + 6, "server\0unit/test\0data", sizeof(msg) - 6);
    assert(send(fd, msg, 8, 0) == 8);
    nanosleep(&pause, NULL);
    assert(send(fd, msg + 8, sizeof(msg) - 8, 0) == (ssize_t)(sizeof(msg) - 8));
    nanosleep(&pause, NULL); close(fd);
    }
    return NULL;
}
int main(void) {
    char absent[100];
    snprintf(absent, sizeof(absent), "/tmp/viart-absent-%ld.sock", (long)getpid());
    viart_client *missing;
    viart_client_options bad = {.endpoint = absent, .name = "viart-missing",
                                 .on_event = error_callback};
    assert(viart_client_create(&bad, &missing) == 0);
    assert(viart_client_start(missing) == 0);
    for (int i = 0; i < 50 && atomic_load(&connect_errors) == 0; i++) {
        struct timespec pause = {.tv_nsec = 10000000}; nanosleep(&pause, NULL);
    }
    viart_client_destroy(missing);
    assert(atomic_load(&connect_errors) > 0);
    char path[100]; snprintf(path, sizeof(path), "/tmp/viart-reactor-%ld.sock", (long)getpid());
    fixture f = {.server = socket(AF_UNIX, SOCK_STREAM, 0), .lock = PTHREAD_MUTEX_INITIALIZER};
    assert(f.server >= 0);
    struct sockaddr_un addr = {.sun_family = AF_UNIX}; strcpy(addr.sun_path, path);
    assert(bind(f.server, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    assert(listen(f.server, 1) == 0);
    pthread_t thread; assert(pthread_create(&thread, NULL, server, &f) == 0);
    viart_client *client;
    viart_client_options opts = {.endpoint = path, .name = "viart-reactor", .on_event = callback,
                                 .user = &f, .reconnect_ms = 30};
    assert(viart_client_create(&opts, &client) == 0);
    assert(viart_client_start(client) == 0);
    for (int i = 0; i < 200; i++) {
        struct timespec pause = {.tv_nsec = 10000000}; nanosleep(&pause, NULL);
        pthread_mutex_lock(&f.lock);
        int done = f.connected == 2 && f.ack == 2 && f.message == 2;
        pthread_mutex_unlock(&f.lock);
        if (done) break;
    }
    viart_client_destroy(client);
    pthread_join(thread, NULL);
    assert(f.connected == 2 && f.ack == 2 && f.message == 2);
    close(f.server); unlink(path); pthread_mutex_destroy(&f.lock);
    return 0;
}
