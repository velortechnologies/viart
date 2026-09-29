#ifndef VIART_HANDSHAKE_H
#define VIART_HANDSHAKE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { VH_GREETING, VH_GREETING_REPLY, VH_REGISTER_REPLY, VH_DONE } viart_handshake;

/* One pure handshake transition. EAGAIN means more input is needed.
 * Caller owns *response; it is NULL if nothing should be sent. */
int viart_handshake_step(viart_handshake *state, const uint8_t *input, size_t input_len,
                         const char *name, size_t *used, uint8_t **response,
                         size_t *response_len, bool *ready);
#endif
