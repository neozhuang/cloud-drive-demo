#define _POSIX_C_SOURCE  200112L
#include "../include/network.h"
#include "../include/common.h"

int tcp_connect(const char * ip, const char * port)
{
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;  

    int ret = getaddrinfo(ip, NULL, &hints, &res);
    if (ret != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        return -1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    ERROR_CHECK(sockfd, -1, "socket");
    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(atoi(port));
    serveraddr.sin_addr = ((struct sockaddr_in *)(res->ai_addr))->sin_addr;

    freeaddrinfo(res);

    ret = connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
    ERROR_CHECK(ret, -1, "connect");

    return sockfd;
}
