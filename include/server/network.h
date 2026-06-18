#pragma once
#include <stdint.h>

int network_listen(const char* host, const char* port);
int network_add_epoll_fd(int epoll_fd, int fd, uint32_t events);
