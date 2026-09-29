#include "handshake.h"
#include "wire.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

int viart_handshake_step(viart_handshake *state, const uint8_t *input, size_t input_len,
                         const char *name, size_t *used, uint8_t **response,
                         size_t *response_len, bool *ready) {
    if (!state || !input || !name || !used || !response || !response_len || !ready)
        return EINVAL;
    *used = 0; *response = NULL; *response_len = 0; *ready = false;
    if (*state == VH_GREETING) {
        if (input_len < 3) return EAGAIN;
        if (input[0] != VW_GREETING || viart_le16(input + 1) != 1) return EPROTO;
        *response = malloc(3);
        if (!*response) return ENOMEM;
        memcpy(*response, input, 3);
        *response_len = *used = 3;
        *state = VH_GREETING_REPLY;
    } else if (*state == VH_GREETING_REPLY) {
        if (input_len < 1) return EAGAIN;
        if (input[0] != VW_RESPONSE_OK) return EPROTO;
        size_t n = strlen(name);
        if (!n || n > UINT16_MAX) return EINVAL;
        *response = malloc(n + 2);
        if (!*response) return ENOMEM;
        viart_store16(*response, (uint16_t)n);
        memcpy(*response + 2, name, n);
        *response_len = n + 2;
        *used = 1;
        *state = VH_REGISTER_REPLY;
    } else if (*state == VH_REGISTER_REPLY) {
        if (input_len < 1) return EAGAIN;
        if (input[0] != VW_RESPONSE_OK) return EPROTO;
        *used = 1; *ready = true; *state = VH_DONE;
    } else return EINVAL;
    return 0;
}
