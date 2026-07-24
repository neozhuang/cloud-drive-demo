#include "client/connection.h"

#include <assert.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    int sockets[2];
    char byte = 'x';

    assert(client_connection_is_alive(-1) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(client_connection_is_alive(sockets[0]) == 1);

    assert(write(sockets[1], &byte, sizeof(byte)) == (ssize_t)sizeof(byte));
    assert(client_connection_is_alive(sockets[0]) == 1);
    assert(read(sockets[0], &byte, sizeof(byte)) == (ssize_t)sizeof(byte));

    close(sockets[1]);
    assert(client_connection_is_alive(sockets[0]) == 0);
    client_connection_close(sockets[0]);

    puts("client connection tests passed");
    return 0;
}
