#define _POSIX_C_SOURCE 200809L
#include "viart.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void on_event(viart_client *client, const viart_event *e, void *user) {
    (void)user;
    switch (e->kind) {
    case VIART_CONNECTED:
        puts("connected");
        /* Subscribe on every connection: replay is intentionally app-owned. */
        viart_client_subscribe(client, "viart/demo", VIART_QOS_PROCESSED, NULL);
        viart_client_publish(client, "viart/demo", "hello from C23", 14,
                             VIART_QOS_PROCESSED, NULL);
        break;
    case VIART_DISCONNECTED:
        if (e->error) fprintf(stderr, "disconnected: %s\n", strerror(e->error));
        else puts("disconnected");
        break;
    case VIART_ACK: printf("ACK id=%u result=0x%02x\n", e->id, e->result); break;
    case VIART_CONNECT_ERROR: fprintf(stderr, "connect failed: %s\n", strerror(e->error)); break;
    case VIART_MESSAGE:
        printf("message sender=%.*s topic=%.*s payload=%.*s\n",
               (int)e->sender.len, e->sender.data, (int)e->topic.len, e->topic.data,
               (int)e->payload.len, e->payload.data);
        break;
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s /path/to/viart.sock client-name\n", argv[0]);
        return 2;
    }
    viart_client *client;
    viart_client_options opts = {.endpoint = argv[1], .name = argv[2], .on_event = on_event};
    int err = viart_client_create(&opts, &client);
    if (!err) err = viart_client_start(client);
    if (err) { fprintf(stderr, "viart: %s\n", strerror(err)); return 1; }
    struct timespec wait = {.tv_sec = 3};
    nanosleep(&wait, NULL);
    viart_client_destroy(client);
    return 0;
}
