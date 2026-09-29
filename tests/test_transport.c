#define _POSIX_C_SOURCE 200809L
#include "transport.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main(void) {
    bool progress;
    assert(viart_transport_connect("bad endpoint", &progress) == -EINVAL);
    assert(viart_transport_connect("127.0.0.1:99999", &progress) == -EINVAL);
    char path[100]; snprintf(path, sizeof(path), "/tmp/viart-transport-%ld.sock", (long)getpid());
    int listener = socket(AF_UNIX, SOCK_STREAM, 0); assert(listener >= 0);
    struct sockaddr_un addr = {.sun_family = AF_UNIX}; strcpy(addr.sun_path, path);
    assert(bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    assert(listen(listener, 1) == 0);
    int fd = viart_transport_connect(path, &progress); assert(fd >= 0);
    int accepted = accept(listener, NULL, NULL); assert(accepted >= 0);
    close(accepted); close(fd); close(listener); unlink(path);

    int tcp = socket(AF_INET, SOCK_STREAM, 0); assert(tcp >= 0);
    struct sockaddr_in inet = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    assert(bind(tcp, (struct sockaddr *)&inet, sizeof(inet)) == 0);
    assert(listen(tcp, 1) == 0);
    socklen_t len = sizeof(inet);
    assert(getsockname(tcp, (struct sockaddr *)&inet, &len) == 0);
    char endpoint[60]; snprintf(endpoint, sizeof(endpoint), "127.0.0.1:%u", ntohs(inet.sin_port));
    fd = viart_transport_connect(endpoint, &progress); assert(fd >= 0);
    accepted = accept(tcp, NULL, NULL); assert(accepted >= 0);
    close(accepted); close(fd); close(tcp);
    return 0;
}
