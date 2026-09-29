#include "queue.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>

static viart_packet *packet(size_t n, uint32_t id) {
    viart_packet *p = calloc(1, sizeof(*p));
    assert(p);
    p->data = malloc(n);
    assert(p->data);
    p->len = n; p->id = id;
    return p;
}
int main(void) {
    viart_queue q;
    assert(viart_queue_init(&q, 10) == 0);
    viart_packet *a = packet(6, 1), *b = packet(4, 2), *over = packet(1, 3);
    assert(viart_queue_push(&q, a) == 0);
    assert(viart_queue_push(&q, b) == 0);
    assert(viart_queue_push(&q, over) == EAGAIN);
    viart_packet_free(over);
    assert(q.bytes == 10);
    assert(viart_queue_pop(&q) == a); viart_packet_free(a);
    assert(viart_queue_pop(&q) == b); viart_packet_free(b);
    assert(viart_queue_pop(&q) == NULL && q.bytes == 0);
    a = packet(1, 4); b = packet(1, 5);
    bool wake = false;
    assert(viart_queue_push_notify(&q, a, &wake) == 0 && wake);
    assert(viart_queue_push_notify(&q, b, &wake) == 0 && !wake);
    over = packet(10, 7);
    wake = true;
    assert(viart_queue_push_notify(&q, over, &wake) == EAGAIN && !wake);
    viart_packet_free(over);
    assert(viart_queue_pop(&q) == a); viart_packet_free(a);
    assert(viart_queue_pop(&q) == b); viart_packet_free(b);
    a = packet(1, 6);
    assert(viart_queue_push_notify(&q, a, &wake) == 0 && wake);
    assert(viart_queue_pop(&q) == a); viart_packet_free(a);
    viart_queue_destroy(&q);
    return 0;
}
