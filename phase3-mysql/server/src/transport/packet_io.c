#include "transport/packet_io.h"

#include <string.h>
#include <sys/socket.h>

static size_t packet_bytes(const ProtocolPacket *packet)
{
    return sizeof(packet->type) + sizeof(packet->status) +
           sizeof(packet->payload_length) + packet->payload_length;
}

int sendn(int sockfd, const void *buffer, size_t length)
{
    const char *cursor = (const char *)buffer;
    size_t sent = 0;

    while (sent < length) {
        ssize_t ret = send(sockfd, cursor + sent, length - sent, 0);

        if (ret <= 0) {
            return -1;
        }

        sent += (size_t)ret;
    }

    return (int)sent;
}

int recvn(int sockfd, void *buffer, size_t length)
{
    char *cursor = (char *)buffer;
    size_t received = 0;

    while (received < length) {
        ssize_t ret = recv(sockfd, cursor + received, length - received, 0);

        if (ret < 0) {
            return -1;
        }

        if (ret == 0) {
            break;
        }

        received += (size_t)ret;
    }

    return (int)received;
}

int send_packet(int sockfd, const ProtocolPacket *packet)
{
    return sendn(sockfd, packet, packet_bytes(packet));
}

int recv_packet(int sockfd, ProtocolPacket *packet)
{
    memset(packet, 0, sizeof(*packet));

    if (recvn(sockfd, &packet->type, sizeof(packet->type)) <= 0) {
        return -1;
    }

    if (recvn(sockfd, &packet->status, sizeof(packet->status)) <= 0) {
        return -1;
    }

    if (recvn(sockfd, &packet->payload_length, sizeof(packet->payload_length)) <= 0) {
        return -1;
    }

    if (packet->payload_length > PROTOCOL_MAX_PAYLOAD_SIZE) {
        return -1;
    }

    if (packet->payload_length > 0 &&
        recvn(sockfd, packet->payload, packet->payload_length) <= 0) {
        return -1;
    }

    return 0;
}
