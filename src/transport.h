#ifndef VIART_TRANSPORT_H
#define VIART_TRANSPORT_H
#include <stdbool.h>

/* Creates a nonblocking, close-on-exec socket. Negative errno on failure. */
int viart_transport_connect(const char *endpoint, bool *in_progress);
int viart_transport_connect_ex(const char *endpoint,const char *token,const char *ca_file,bool *in_progress);
#endif
