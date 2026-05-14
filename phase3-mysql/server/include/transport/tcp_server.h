#ifndef CLOUD_DRIVE_TRANSPORT_TCP_SERVER_H
#define CLOUD_DRIVE_TRANSPORT_TCP_SERVER_H

#include <stdint.h>

#include "config/config.h"
#include "infra/thread_pool/thread_pool.h"

#define NET_MAX_EVENTS 16

int net_create_listen_socket(const ListenConfig *config);
int net_add_epoll_fd(int epoll_fd, int fd, uint32_t events);
int net_drain_shutdown_fd(int shutdown_fd);
void net_accept_pending_clients(int epoll_fd, int listen_fd);
void net_handle_client_packets(int epoll_fd, int client_fd, ThreadPool *pool);
void net_close_client(int epoll_fd, int client_fd);

#endif
