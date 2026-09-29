#ifndef VIART_WS_BRIDGE_H
#define VIART_WS_BRIDGE_H
/* A socketpair-backed WS/WSS adapter keeps the existing epoll reactor and
 * VIART parser unchanged. Returns a nonblocking fd or negative errno. */
int viart_ws_bridge_open(const char *endpoint,const char *token,const char *ca_file);
#endif
