#define _POSIX_C_SOURCE 200809L
#include "viart.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct { pthread_mutex_t lock; int connected, ack_ok, messages, broadcasts; } fixture;
static void callback(viart_client *client, const viart_event *e, void *arg) {
    (void)client;
    fixture *f = arg;
    pthread_mutex_lock(&f->lock);
    if (e->kind == VIART_CONNECTED) f->connected++;
    else if (e->kind == VIART_ACK && e->result == 1) f->ack_ok++;
    else if (e->kind == VIART_MESSAGE && e->topic.len == 13 &&
             memcmp(e->topic.data, "viart/testing", 13) == 0 &&
             e->payload.len == 4 && memcmp(e->payload.data, "ping", 4) == 0) f->messages++;
    else if (e->kind == VIART_MESSAGE && e->frame_kind == 0x13 &&
             e->payload.len == 4 && memcmp(e->payload.data, "cast", 4) == 0) f->broadcasts++;
    pthread_mutex_unlock(&f->lock);
}
static int wait_count(fixture *f, int kind, int target) {
    for (int i = 0; i < 200; i++) {
        pthread_mutex_lock(&f->lock);
        int value = kind == 0 ? f->connected : kind == 1 ? f->ack_ok :
                    kind == 2 ? f->messages : f->broadcasts;
        pthread_mutex_unlock(&f->lock);
        if (value >= target) return 0;
        struct timespec pause = {.tv_nsec = 10000000}; nanosleep(&pause, NULL);
    }
    return 1;
}
int main(void) {
    const char *socket_path = getenv("VIART_TEST_SOCKET");
    if (!socket_path) socket_path = "/tmp/viart-test.sock";
    fixture f = {.lock = PTHREAD_MUTEX_INITIALIZER};
    char name_a[64], name_b[64];
    snprintf(name_a, sizeof(name_a), "viart.test.sub.%ld", (long)getpid());
    snprintf(name_b, sizeof(name_b), "viart.test.pub.%ld", (long)getpid());
    viart_client *a, *b;
    viart_client_options oa = {.endpoint = socket_path, .name = name_a, .on_event = callback, .user = &f};
    viart_client_options ob = {.endpoint = socket_path, .name = name_b, .on_event = callback, .user = &f};
    assert(viart_client_create(&oa, &a) == 0);
    assert(viart_client_create(&ob, &b) == 0);
    assert(viart_client_start(a) == 0);
    assert(viart_client_start(b) == 0);
    assert(wait_count(&f, 0, 2) == 0);
    assert(viart_client_subscribe(a, "viart/testing", VIART_QOS_PROCESSED, NULL) == 0);
    assert(wait_count(&f, 1, 1) == 0);
    assert(viart_client_publish(b, "viart/testing", "ping", 4, VIART_QOS_PROCESSED, NULL) == 0);
    assert(wait_count(&f, 1, 2) == 0);
    assert(wait_count(&f, 2, 1) == 0);
    assert(viart_client_exclude(a, "viart/testing", VIART_QOS_PROCESSED, NULL) == 0);
    assert(wait_count(&f, 1, 3) == 0);
    assert(viart_client_publish(b, "viart/testing", "ping", 4, VIART_QOS_PROCESSED, NULL) == 0);
    assert(wait_count(&f, 1, 4) == 0);
    struct timespec pause = {.tv_nsec = 200000000}; nanosleep(&pause, NULL);
    pthread_mutex_lock(&f.lock);
    assert(f.messages == 1);
    pthread_mutex_unlock(&f.lock);
    assert(viart_client_unexclude(a, "viart/testing", VIART_QOS_PROCESSED, NULL) == 0);
    assert(wait_count(&f, 1, 5) == 0);
    assert(viart_client_publish(b, "viart/testing", "ping", 4, VIART_QOS_PROCESSED, NULL) == 0);
    assert(wait_count(&f, 1, 6) == 0);
    assert(wait_count(&f, 2, 2) == 0);
    assert(viart_client_publish_for(b, "viart/testing", name_a, "ping", 4,
                                    VIART_QOS_PROCESSED, NULL) == 0);
    assert(wait_count(&f, 1, 7) == 0);
    assert(wait_count(&f, 2, 3) == 0);
    assert(viart_client_broadcast(b, "viart.test.*", "cast", 4, VIART_QOS_PROCESSED, NULL) == 0);
    assert(wait_count(&f, 1, 8) == 0);
    assert(wait_count(&f, 3, 2) == 0);
    viart_client_destroy(b); viart_client_destroy(a);
    pthread_mutex_destroy(&f.lock);
    return 0;
}
