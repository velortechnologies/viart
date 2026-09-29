#include "protocol.h"
#include "wire.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    uint8_t *wire = NULL; size_t len = 0, used = 0;
    assert(viart_wire_encode(VW_PUBLISH, 1, 42, "a/b", "ping", 4, &wire, &len) == 0);
    bp_command c;
    assert(bp_parse(wire, 8, 1024, &c, &used) == EAGAIN);
    assert(bp_parse(wire, len, 1024, &c, &used) == 0);
    assert(used == len && c.id == 42 && c.op == BP_PUBLISH && c.qos == 1);
    assert(c.target_len == 3 && !memcmp(c.target, "a/b", 3));
    assert(c.payload_len == 4 && !memcmp(c.payload, "ping", 4));
    free(wire);
    size_t dlen = 0;
    uint8_t *d = bp_delivery(BP_PUBLISH, false, "sender", (uint8_t *)"a/b", 3,
                             (uint8_t *)"ping", 4, &dlen);
    assert(d && dlen == 6 + 7 + 4 + 4);
    viart_wire_in decoded;
    assert(viart_wire_decode(d, dlen, &decoded) == 0);
    assert(decoded.topic.len == 3 && !memcmp(decoded.topic.data, "a/b", 3));
    free(d);
    return 0;
}
