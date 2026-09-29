#ifndef VIART_PENDING_H
#define VIART_PENDING_H
#include <stdbool.h>
#include <stdint.h>

typedef struct viart_pending {
    struct viart_pending *next;
    uint32_t id;
    int64_t deadline_ms;
} viart_pending;

int viart_pending_add(viart_pending **head, uint32_t id, int64_t deadline_ms);
bool viart_pending_take(viart_pending **head, uint32_t id);
viart_pending *viart_pending_extract_expired(viart_pending **head, int64_t now_ms);
viart_pending *viart_pending_take_all(viart_pending **head);
void viart_pending_free_list(viart_pending *head);
#endif
