#ifndef VIART_QUEUE_H
#define VIART_QUEUE_H
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct viart_packet {
    struct viart_packet *next;
    uint8_t *data;
    size_t len, offset;
    uint32_t id;
    uint8_t qos;
} viart_packet;

typedef struct {
    pthread_mutex_t mutex;
    viart_packet *head, *tail;
    size_t bytes, limit;
} viart_queue;

int viart_queue_init(viart_queue *q, size_t limit);
void viart_queue_destroy(viart_queue *q);
int viart_queue_push(viart_queue *q, viart_packet *packet);
int viart_queue_push_notify(viart_queue *q, viart_packet *packet, bool *became_nonempty);
viart_packet *viart_queue_pop(viart_queue *q);
void viart_packet_free(viart_packet *packet);

#endif
