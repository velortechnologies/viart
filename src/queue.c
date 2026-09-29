#include "queue.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int viart_queue_init(viart_queue *q, size_t limit) {
    if (!q || !limit) return EINVAL;
    memset(q, 0, sizeof(*q));
    q->limit = limit;
    return pthread_mutex_init(&q->mutex, NULL);
}
void viart_packet_free(viart_packet *p) {
    if (p) { free(p->data); free(p); }
}
void viart_queue_destroy(viart_queue *q) {
    if (!q) return;
    pthread_mutex_lock(&q->mutex);
    viart_packet *p = q->head;
    while (p) { viart_packet *next = p->next; viart_packet_free(p); p = next; }
    q->head = q->tail = NULL; q->bytes = 0;
    pthread_mutex_unlock(&q->mutex);
    pthread_mutex_destroy(&q->mutex);
}
int viart_queue_push_notify(viart_queue *q, viart_packet *p, bool *became_nonempty) {
    if (!q || !p) return EINVAL;
    if (became_nonempty) *became_nonempty = false;
    pthread_mutex_lock(&q->mutex);
    if (p->len > q->limit - q->bytes) {
        pthread_mutex_unlock(&q->mutex);
        return EAGAIN;
    }
    bool was_empty = q->head == NULL;
    p->next = NULL;
    if (q->tail) q->tail->next = p; else q->head = p;
    q->tail = p; q->bytes += p->len;
    if (became_nonempty) *became_nonempty = was_empty;
    pthread_mutex_unlock(&q->mutex);
    return 0;
}
int viart_queue_push(viart_queue *q, viart_packet *p) {
    return viart_queue_push_notify(q, p, NULL);
}
viart_packet *viart_queue_pop(viart_queue *q) {
    pthread_mutex_lock(&q->mutex);
    viart_packet *p = q->head;
    if (p) {
        q->head = p->next;
        if (!q->head) q->tail = NULL;
        q->bytes -= p->len;
        p->next = NULL;
    }
    pthread_mutex_unlock(&q->mutex);
    return p;
}
