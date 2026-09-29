#include "wire.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    uint8_t *buf; size_t len;
    assert(viart_wire_encode(VW_PUBLISH, VIART_QOS_PROCESSED, 0x12345678,
                             "topic", "a\0b", 3, &buf, &len) == 0);
    assert(len == 9 + 5 + 1 + 3);
    assert(viart_le32(buf) == 0x12345678);
    assert(buf[4] == (VW_PUBLISH | 0x40));
    assert(viart_le32(buf + 5) == 9);
    assert(memcmp(buf + 9, "topic\0a\0b", 9) == 0);
    free(buf);
    assert(viart_wire_encode(VW_SUBSCRIBE, 0, 1, "topic", NULL, 0, &buf, &len) == 0);
    assert(len == 14 && viart_le32(buf + 5) == 5);
    free(buf);
    assert(viart_wire_encode(VW_PUBLISH, 4, 1, "x", NULL, 0, &buf, &len) == EINVAL);
    assert(viart_wire_encode(VW_PUBLISH, 0, 1, "", NULL, 0, &buf, &len) == EINVAL);

    uint8_t ack[6] = {VW_ACK, 0, 0, 0, 0, 1};
    viart_store32(ack + 1, 99);
    viart_wire_in in;
    assert(viart_wire_decode(ack, 6, &in) == 0);
    assert(in.kind == VW_ACK && in.id_or_len == 99 && in.flag == 1);
    uint8_t publication[6 + 3 + 1 + 4 + 1 + 2] = {VW_PUBLISH};
    viart_store32(publication + 1, sizeof(publication) - 6);
    memcpy(publication + 6, "bob\0news\0ok", sizeof(publication) - 6);
    assert(viart_wire_decode(publication, sizeof(publication), &in) == 0);
    assert(in.sender.len == 3 && in.topic.len == 4 && in.payload.len == 2);
    assert(memcmp(in.payload.data, "ok", 2) == 0);
    publication[6 + 3] = 'X';
    assert(viart_wire_decode(publication, sizeof(publication), &in) == EPROTO);
    return 0;
}
