#include "transport/tcp_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dispatch/packet_router.h"
#include "protocol/protocol.h"
#include "session/session.h"
#include "transport/packet_io.h"

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0) {
        return -1;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void net_close_client(int epoll_fd, int client_fd)
{
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
    session_destroy(client_fd);
    close(client_fd);
}

int net_add_epoll_fd(int epoll_fd, int fd, uint32_t events)
{
    struct epoll_event event;

    memset(&event, 0, sizeof(event));
    event.events = events;
    event.data.fd = fd;

    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
}

int net_create_listen_socket(const ListenConfig *config)
{
    int listen_fd;
    int reuse_addr = 1;
    struct sockaddr_in address;

    if (config == NULL) {
        return -1;
    }

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("Failed to create listen socket");
        return -1;
    }

    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr,
                   sizeof(reuse_addr)) != 0) {
        perror("Failed to set SO_REUSEADDR");
        close(listen_fd);
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)config->port);
    if (inet_pton(AF_INET, config->host, &address.sin_addr) != 1) {
        fprintf(stderr, "Invalid listen host: %s\n", config->host);
        close(listen_fd);
        return -1;
    }

    if (bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        perror("Failed to bind listen socket");
        close(listen_fd);
        return -1;
    }

    if (set_nonblocking(listen_fd) != 0) {
        perror("Failed to set listen socket nonblocking");
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, config->backlog) != 0) {
        perror("Failed to listen on socket");
        close(listen_fd);
        return -1;
    }

    return listen_fd;
}

int net_drain_shutdown_fd(int shutdown_fd)
{
    char buffer[32];
    ssize_t read_count;

    do {
        read_count = read(shutdown_fd, buffer, sizeof(buffer));
    } while (read_count < 0 && errno == EINTR);

    return read_count >= 0 ? 0 : -1;
}

void net_accept_pending_clients(int epoll_fd, int listen_fd)
{
    for (;;) {
        int client_fd;
        struct sockaddr_in client_address;
        socklen_t client_address_len = sizeof(client_address);
        char client_host[INET_ADDRSTRLEN];

        client_fd = accept(listen_fd, (struct sockaddr *)&client_address,
                           &client_address_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }

            if (errno == EINTR) {
                continue;
            }

            perror("Failed to accept client connection");
            return;
        }

        if (inet_ntop(AF_INET, &client_address.sin_addr, client_host,
                      sizeof(client_host)) == NULL) {
            snprintf(client_host, sizeof(client_host), "unknown");
        }

        if (session_create(client_fd) == NULL) {
            perror("Failed to allocate client session");
            close(client_fd);
            continue;
        }

        if (net_add_epoll_fd(epoll_fd, client_fd, EPOLLIN | EPOLLRDHUP) != 0) {
            perror("Failed to add client fd to epoll");
            session_destroy(client_fd);
            close(client_fd);
            continue;
        }

        printf("Accepted client fd %d from %s:%d\n", client_fd, client_host,
               ntohs(client_address.sin_port));
    }
}

void net_handle_client_packets(int epoll_fd, int client_fd, ThreadPool *pool)
{
    ProtocolPacket packet;
    PacketTaskContext *context;

    if (recv_packet(client_fd, &packet) != 0) {
        net_close_client(epoll_fd, client_fd);
        return;
    }

    context = packet_task_context_create(client_fd, &packet);
    if (context == NULL) {
        perror("Failed to allocate client packet task context");
        return;
    }

    if (thread_pool_submit(pool, packet_router_handle_task, context) != 0) {
        perror("Failed to submit client packet task");
        packet_task_context_destroy(context);
    }
}
