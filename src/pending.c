#include "pending.h"
#include <errno.h>
#include <stdlib.h>

int viart_pending_add(viart_pending **head, uint32_t id, int64_t deadline_ms) {
    if (!head || !id) return EINVAL;
    viart_pending *p = malloc(sizeof(*p));
    if (!p) return ENOMEM;
    *p = (viart_pending){.next = *head, .id = id, .deadline_ms = deadline_ms};
    *head = p;
    return 0;
}
bool viart_pending_take(viart_pending **head, uint32_t id) {
    if (!head) return false;
    for (viart_pending **pp = head; *pp; pp = &(*pp)->next) {
        if ((*pp)->id == id) {
            viart_pending *p = *pp; *pp = p->next; free(p);
            return true;
        }
    }
    return false;
}
viart_pending *viart_pending_extract_expired(viart_pending **head, int64_t now_ms) {
    if (!head) return NULL;
    viart_pending *expired = NULL;
    viart_pending **pp = head;
    while (*pp) {
        viart_pending *p = *pp;
        if (p->deadline_ms > now_ms) { pp = &p->next; continue; }
        *pp = p->next; p->next = expired; expired = p;
    }
    return expired;
}
viart_pending *viart_pending_take_all(viart_pending **head) {
    if (!head) return NULL;
    viart_pending *p = *head; *head = NULL; return p;
}
void viart_pending_free_list(viart_pending *head) {
    while (head) { viart_pending *next = head->next; free(head); head = next; }
}
