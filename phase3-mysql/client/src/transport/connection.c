#include "transport/connection.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int client_net_connect(const ClientServerConfig *config, ClientContext *context)
{
    int fd;
    struct sockaddr_in address;

    if (config == NULL) {
        errno = EINVAL;
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)config->port);
    if (inet_pton(AF_INET, config->host, &address.sin_addr) != 1) {
        fprintf(stderr, "Invalid server host: %s\n", config->host);
        close(fd);
        errno = EINVAL;
        return -1;
    }
    context->state = CLIENT_STATE_CONNECTING;
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    context->state = CLIENT_STATE_CONNECTED;
    context->sockfd = fd;

    return fd;
}

void client_net_close(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}
