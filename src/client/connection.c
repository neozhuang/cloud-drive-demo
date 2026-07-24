#include "client/connection.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static int64_t monotonic_milliseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return -1;
    }
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int wait_for_connect(int fd, int timeout_ms)
{
    struct pollfd poll_fd;
    int64_t deadline;

    deadline = monotonic_milliseconds();
    if (deadline < 0) {
        return -1;
    }
    deadline += timeout_ms;

    poll_fd.fd = fd;
    poll_fd.events = POLLOUT;
    poll_fd.revents = 0;

    for (;;) {
        int64_t now = monotonic_milliseconds();
        int remaining;
        int result;
        int socket_error = 0;
        socklen_t error_size = sizeof(socket_error);

        if (now < 0 || now >= deadline) {
            return -1;
        }
        remaining = (int)(deadline - now);
        result = poll(&poll_fd, 1, remaining);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return -1;
        }
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error,
                       &error_size) != 0 || socket_error != 0) {
            if (socket_error != 0) {
                errno = socket_error;
            }
            return -1;
        }
        return 0;
    }
}

static int connect_address(const struct addrinfo *address, int timeout_ms)
{
    int fd;
    int flags;
    int result;

    fd = socket(address->ai_family, address->ai_socktype,
                address->ai_protocol);
    if (fd < 0) {
        return -1;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        close(fd);
        return -1;
    }

    result = connect(fd, address->ai_addr, address->ai_addrlen);
    if (result != 0 && errno == EINPROGRESS) {
        result = wait_for_connect(fd, timeout_ms);
    }
    if (result != 0 || fcntl(fd, F_SETFL, flags) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

int client_connection_open(const char *host,
                           const char *port,
                           int connect_timeout_ms,
                           int io_timeout_ms)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    struct timeval timeout;
    int fd = -1;

    if (host == NULL || host[0] == '\0' || port == NULL || port[0] == '\0' ||
        connect_timeout_ms <= 0 || io_timeout_ms <= 0) {
        return -1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host, port, &hints, &addresses) != 0) {
        return -1;
    }

    for (address = addresses; address != NULL; address = address->ai_next) {
        fd = connect_address(address, connect_timeout_ms);
        if (fd >= 0) {
            break;
        }
    }
    freeaddrinfo(addresses);
    if (fd < 0) {
        return -1;
    }

    timeout.tv_sec = io_timeout_ms / 1000;
    timeout.tv_usec = (io_timeout_ms % 1000) * 1000;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

int client_connection_is_alive(int fd)
{
    char byte;

    if (fd < 0) {
        return 0;
    }
    for (;;) {
        ssize_t received = recv(fd, &byte, sizeof(byte),
                                MSG_PEEK | MSG_DONTWAIT);

        if (received > 0) {
            return 1;
        }
        if (received == 0) {
            return 0;
        }
        if (errno == EINTR) {
            continue;
        }
        return errno == EAGAIN || errno == EWOULDBLOCK;
    }
}

void client_connection_close(int fd)
{
    if (fd < 0) {
        return;
    }

    shutdown(fd, SHUT_RDWR);
    close(fd);
}
