#include "rpc_wire.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    uint8_t *data; size_t len; viart_rpc_frame f;
    assert(viart_rpc_encode_request(42, "test", "xyz", 3, &data, &len)==0);
    assert(viart_rpc_parse(data,len,&f)==0);
    assert(f.kind==VR_REQUEST&&f.id==42&&f.method_len==4&&f.payload_len==3);
    assert(!memcmp(f.method,"test",4)&&!memcmp(f.payload,"xyz",3));free(data);
    assert(viart_rpc_encode_reply(VR_ERROR,42,-32601,"missing",7,&data,&len)==0);
    assert(viart_rpc_parse(data,len,&f)==0&&f.error_code==-32601&&f.payload_len==7);free(data);
    assert(viart_rpc_parse((uint8_t *)"\x01\0\0\0\0abc",8,&f)==EPROTO);
    return 0;
}
