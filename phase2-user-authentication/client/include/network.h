#ifndef __NETWORK_H__
#define __NETWORK_H__
// 网络编程
#include <sys/socket.h> // socket
#include <netinet/in.h> // struct sockaddr_in
#include <arpa/inet.h>  // inet_pton
#include <sys/types.h>
#include <netdb.h>

int tcp_connect(const char * ip, const char * port);

#endif /* __NETWORK_H__ */
