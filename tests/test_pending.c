#include "pending.h"
#include <assert.h>
#include <stddef.h>

int main(void) {
    viart_pending *head = NULL;
    assert(viart_pending_add(&head, 1, 10) == 0);
    assert(viart_pending_add(&head, 2, 20) == 0);
    assert(viart_pending_add(&head, 3, 30) == 0);
    assert(viart_pending_take(&head, 2));
    assert(!viart_pending_take(&head, 2));
    assert(viart_pending_extract_expired(&head, 9) == NULL);
    viart_pending *expired = viart_pending_extract_expired(&head, 10);
    assert(expired && expired->id == 1 && !expired->next);
    viart_pending_free_list(expired);
    assert(head && head->id == 3 && !head->next);
    viart_pending_free_list(viart_pending_take_all(&head));
    assert(head == NULL);
    return 0;
}
