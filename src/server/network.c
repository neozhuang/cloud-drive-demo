#include "server/network.h"

#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <netdb.h>
#include <string.h>
#include <sys/epoll.h>


// todo: set_nonblocking
int network_listen(const char* host, const char* port)
{
    struct addrinfo hints;
    struct addrinfo *res;
    struct addrinfo *rp;

    int listenfd = -1;
    int ret;

    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_UNSPEC;      // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP
    hints.ai_flags = AI_PASSIVE;

    ret = getaddrinfo(host,
                      port,
                      &hints,
                      &res);

    if (ret != 0)
    {
        fprintf(stderr,
                "getaddrinfo: %s\n",
                gai_strerror(ret));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next)
    {
        listenfd = socket(rp->ai_family,
                          rp->ai_socktype,
                          rp->ai_protocol);

        if (listenfd == -1)
            continue;

        int opt = 1;

        /*
         * SO_REUSEADDR:
         * allow port to use again quickly.
         *
         * case:
         *   when server exit and start,
         *   port maybe still stuck at TIME_WAIT status.
         *   if not open this, will show ``Address already in use''
         */
        setsockopt(listenfd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &opt,
                   sizeof(opt));

        if (bind(listenfd,
                 rp->ai_addr,
                 rp->ai_addrlen) == 0)
        {
            break;
        }
        perror("bind");
        close(listenfd);
        listenfd = -1;
    }

    freeaddrinfo(res);
    
    if (listenfd == -1)
        return -1;

    // server config file backlog reserved for future extension
    if (listen(listenfd, SOMAXCONN) == -1)
    {
        perror("listen");
        close(listenfd);
        return -1;
    }

    return listenfd;
}

int network_add_epoll_fd(int epoll_fd, int fd, uint32_t events)
{
    struct epoll_event event;

    memset(&event, 0, sizeof(event));
    event.events = events;
    event.data.fd = fd;

    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
}
