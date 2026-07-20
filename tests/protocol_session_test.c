#include "common/protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void test_packet_round_trip(void)
{
    int sockets[2];
    const char payload[] = "hello";
    session_id_t session_id;
    packet_t outgoing;
    packet_t incoming;

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    for (size_t i = 0; i < SESSION_ID_SIZE; ++i) {
        session_id.bytes[i] = (unsigned char)(i + 1U);
    }

    assert(packet_init(&outgoing, CMD_PWD, STATUS_OK, &session_id,
                       payload, sizeof(payload) - 1U) == 0);
    assert(protocol_send_packet(sockets[0], &outgoing) == 0);
    assert(protocol_recv_packet(sockets[1], &incoming) == 0);

    assert(incoming.header.type == CMD_PWD);
    assert(incoming.header.status == STATUS_OK);
    assert(incoming.header.payload_len == sizeof(payload) - 1U);
    assert(session_id_equal(&incoming.header.session_id, &session_id));
    assert(incoming.owns_payload);
    assert(memcmp(incoming.payload, payload, sizeof(payload) - 1U) == 0);

    packet_release(&outgoing);
    packet_release(&incoming);
    close(sockets[0]);
    close(sockets[1]);
}

static void test_anonymous_packet(void)
{
    int sockets[2];
    packet_t outgoing;
    packet_t incoming;

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(packet_init(&outgoing, CMD_LOGIN_REQ, STATUS_OK, NULL,
                       NULL, 0U) == 0);
    assert(session_id_is_empty(&outgoing.header.session_id));

    assert(protocol_send_packet(sockets[0], &outgoing) == 0);
    assert(protocol_recv_packet(sockets[1], &incoming) == 0);
    assert(incoming.header.type == CMD_LOGIN_REQ);
    assert(incoming.header.payload_len == 0U);
    assert(session_id_is_empty(&incoming.header.session_id));
    assert(!incoming.owns_payload);

    packet_release(&outgoing);
    packet_release(&incoming);
    close(sockets[0]);
    close(sockets[1]);
}

static void test_invalid_payload(void)
{
    packet_t packet;

    assert(packet_init(&packet, CMD_PWD, STATUS_OK, NULL, NULL, 1U) == -1);
}

int main(void)
{
    test_packet_round_trip();
    test_anonymous_packet();
    test_invalid_payload();

    puts("protocol session-id tests passed");
    return 0;
}
