#include "client/network.h"
#include "client/state.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>

int network_connect(client_state_t* state, const char* server_ip, const char* server_port)
{
    struct addrinfo hints;
    struct addrinfo *res;
    struct addrinfo *rp;
    int sock_fd;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP

    int ret = getaddrinfo(server_ip, server_port, &hints, &res);
    if (ret != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (sock_fd == -1)
            continue;

        if (connect(sock_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            printf("connected\n");
            break;
        }

        close(sock_fd); // current sock_fd connection failed, close it
    }

    freeaddrinfo(res);

    if (rp == NULL) {
        fprintf(stderr, "could not connect\n");
        return -1;
    }
    state->sock_fd = sock_fd;
    state->status = CLIENT_STATE_CONNECTED;
    return sock_fd;
}

