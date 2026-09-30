#include "transport.h"
#ifndef VIART_WITH_WS
#define VIART_WITH_WS 1
#endif
#if VIART_WITH_WS
#include "ws_bridge.h"
#endif
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int viart_transport_connect(const char *endpoint, bool *in_progress) {
    if (!endpoint || !in_progress) return -EINVAL;
    *in_progress = false;
    int fd;
    if (endpoint[0] == '/') {
        struct sockaddr_un addr = {.sun_family = AF_UNIX};
        if (strlen(endpoint) >= sizeof(addr.sun_path)) return -ENAMETOOLONG;
        strcpy(addr.sun_path, endpoint);
        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0) return -errno;
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) return fd;
    } else {
        const char *colon = strrchr(endpoint, ':');
        if (!colon || colon == endpoint || !colon[1] || (size_t)(colon - endpoint) >= 64)
            return -EINVAL;
        char host[64];
        memcpy(host, endpoint, (size_t)(colon - endpoint));
        host[colon - endpoint] = 0;
        char *end;
        long port = strtol(colon + 1, &end, 10);
        if (*end || port < 1 || port > 65535) return -EINVAL;
        struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port)};
        if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) return -EINVAL;
        fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0) return -errno;
        int one = 1;
        (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) return fd;
    }
    int err = errno;
    if (err == EINPROGRESS) { *in_progress = true; return fd; }
    close(fd);
    return -err;
}
int viart_transport_connect_ex(const char *endpoint,const char *token,const char *ca_file,bool *in_progress){
 if(!endpoint||!in_progress)return -EINVAL;
 if(!strncmp(endpoint,"ws://",5)||!strncmp(endpoint,"wss://",6)){
  *in_progress=false;
#if VIART_WITH_WS
  return viart_ws_bridge_open(endpoint,token,ca_file);
#else
  (void)token;(void)ca_file;return -EOPNOTSUPP;
#endif
 }
 return viart_transport_connect(endpoint,in_progress);
}
