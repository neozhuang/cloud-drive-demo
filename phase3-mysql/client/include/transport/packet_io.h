#ifndef CLIENT_TRANSPORT_PACKET_IO_H
#define CLIENT_TRANSPORT_PACKET_IO_H

#include <stddef.h>

#include "protocol/protocol.h"

int sendn(int sockfd, const void *buffer, size_t length);
int recvn(int sockfd, void *buffer, size_t length);
int send_packet(int sockfd, const ProtocolPacket *packet);
int recv_packet(int sockfd, ProtocolPacket *packet);

#endif
