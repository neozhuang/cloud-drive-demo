#ifndef __NETWORK_H__
#define __NETWORK_H__

int tcp_init(const char * ip, int port);

int add_epoll_readfd(int epfd, int fd);

int del_epoll_readfd(int epfd, int fd);

#endif /* __NETWORK_H__ */
