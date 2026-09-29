#include "handshake.h"
#include "wire.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    viart_handshake state = VH_GREETING;
    uint8_t hello[3] = {0xEB, 1, 0};
    size_t used, response_len; uint8_t *response; bool ready;
    assert(viart_handshake_step(&state, hello, 1, "alice", &used, &response,
                                 &response_len, &ready) == EAGAIN);
    assert(state == VH_GREETING);
    assert(viart_handshake_step(&state, hello, 3, "alice", &used, &response,
                                 &response_len, &ready) == 0);
    assert(used == 3 && response_len == 3 && memcmp(response, hello, 3) == 0 && !ready);
    free(response);
    uint8_t ok = 1;
    assert(viart_handshake_step(&state, &ok, 1, "alice", &used, &response,
                                 &response_len, &ready) == 0);
    assert(state == VH_REGISTER_REPLY && used == 1 && response_len == 7);
    assert(viart_le16(response) == 5 && memcmp(response + 2, "alice", 5) == 0);
    free(response);
    assert(viart_handshake_step(&state, &ok, 1, "alice", &used, &response,
                                 &response_len, &ready) == 0);
    assert(state == VH_DONE && ready && used == 1 && response == NULL);
    state = VH_GREETING;
    hello[2] = 2;
    assert(viart_handshake_step(&state, hello, 3, "alice", &used, &response,
                                 &response_len, &ready) == EPROTO);
    return 0;
}
