#ifndef VIART_RPC_WIRE_H
#define VIART_RPC_WIRE_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint8_t kind;
    uint32_t id;
    int16_t error_code;
    const uint8_t *method, *payload;
    size_t method_len, payload_len;
} viart_rpc_frame;
enum { VR_NOTIFICATION=0, VR_REQUEST=1, VR_REPLY=0x11, VR_ERROR=0x12 };
int viart_rpc_parse(const uint8_t *data, size_t len, viart_rpc_frame *out);
int viart_rpc_encode_request(uint32_t id, const char *method, const void *params,
                             size_t params_len, uint8_t **out, size_t *out_len);
int viart_rpc_encode_notification(const void *data, size_t len,
                                  uint8_t **out, size_t *out_len);
int viart_rpc_encode_reply(uint8_t kind, uint32_t id, int16_t error_code,
                           const void *data, size_t len, uint8_t **out, size_t *out_len);
#endif
