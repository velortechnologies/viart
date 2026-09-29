#include "config.h"
#include <assert.h>
int main(void) {
    bp_config c;
    char *ok[]={"viartd","-B","/tmp/test.sock","--max-clients","2","--idle-ms","100"};
    assert(bp_config_parse(7,ok,&c)==0);
    assert(c.max_clients==2 && c.idle_ms==100 && c.max_queue==4u*1024u*1024u);
    char *bad[]={"viartd","-B","/tmp/test.sock","--max-clients","0"};
    assert(bp_config_parse(5,bad,&c)!=0);
    return 0;
}
