#include "common/protocol.h"

#include <string.h>
#include <sys/socket.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdlib.h>

const char *cmd_type_to_str(cmd_type_t type)
{
    static const char *names[CMD_ERROR + 1] = {
        [CMD_LOGIN_REQ] = "login", [CMD_PWD] = "pwd", [CMD_CD] = "cd",
        [CMD_LS] = "ls", [CMD_LL] = "ll", [CMD_TREE] = "tree",
        [CMD_RM] = "rm", [CMD_CAT] = "cat", [CMD_MKDIR] = "mkdir",
        [CMD_RMDIR] = "rmdir", [CMD_PUTS_REQ] = "puts",
        [CMD_GETS_REQ] = "gets", [CMD_PUTS_RESP] = "puts_resp",
        [CMD_GETS_RESP] = "gets_resp", [CMD_RESUME_POS] = "resume_pos",
        [CMD_FILE_DATA] = "file_data", [CMD_FILE_END] = "file_end",
        [CMD_ACK] = "ack", [CMD_ERROR] = "error"
    };

    return type > CMD_INVALID && type <= CMD_ERROR && names[type] != NULL
        ? names[type] : "invalid";
}


/*
 * host_to_net_u64 / net_to_host_u64:
 * transfer 64 int to and from between host and net bytes order
 */
uint64_t host_to_net_u64(uint64_t value)
{
    uint32_t high = htonl((uint32_t)(value >> 32));
    uint32_t low = htonl((uint32_t)(value & 0xffffffffU));

    return ((uint64_t)low << 32) | high;
}

uint64_t net_to_host_u64(uint64_t value) {
    uint32_t high = ntohl((uint32_t)(value >> 32));
    uint32_t low = ntohl((uint32_t)(value & 0xffffffffU));

    return ((uint64_t)low << 32) | high;
}


int send_n(int fd, const void *buf, size_t len)
{
    const char *ptr = (const char *)buf;
    size_t total = 0;

    while (total < len) {
        ssize_t sent = send(fd, ptr + total, len - total, 0);

        if (sent < 0) {
            // interpreted by signal, wait a moment and continue
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        total += (size_t)sent;
    }

    return 0;
}

int recv_n(int fd, void *buf, size_t len)
{
    char *ptr = (char *)buf;
    size_t total = 0;

    while (total < len) {
        ssize_t recved = recv(fd, ptr + total, len - total, 0);

        if (recved < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        // recv return 0: peer closed
        if (recved == 0) {
            return -1;
        }

        total += (size_t)recved;
    }

    return 0;
}

enum {
    WIRE_MAGIC_OFFSET = 0,
    WIRE_VERSION_OFFSET = 4,
    WIRE_TYPE_OFFSET = 8,
    WIRE_STATUS_OFFSET = 12,
    WIRE_PAYLOAD_LEN_OFFSET = 16,
    WIRE_SESSION_ID_OFFSET = 20
};

static void write_net_u32(unsigned char *dst, uint32_t value)
{
    uint32_t net_value = htonl(value);
    memcpy(dst, &net_value, sizeof(net_value));
}

static uint32_t read_net_u32(const unsigned char *src)
{
    uint32_t net_value;
    memcpy(&net_value, src, sizeof(net_value));
    return ntohl(net_value);
}

static void encode_wire_header(
    unsigned char wire_header[TLV_WIRE_HEADER_SIZE],
    const packet_header_t *header)
{
    memset(wire_header, 0, TLV_WIRE_HEADER_SIZE);
    write_net_u32(wire_header + WIRE_MAGIC_OFFSET, TLV_MAGIC);
    write_net_u32(wire_header + WIRE_VERSION_OFFSET, TLV_VERSION);
    write_net_u32(wire_header + WIRE_TYPE_OFFSET, (uint32_t)header->type);
    write_net_u32(wire_header + WIRE_STATUS_OFFSET, (uint32_t)header->status);
    write_net_u32(wire_header + WIRE_PAYLOAD_LEN_OFFSET, header->payload_len);
    memcpy(wire_header + WIRE_SESSION_ID_OFFSET,
           header->session_id.bytes,
           SESSION_ID_SIZE);
}

int packet_init(packet_t *packet,
                cmd_type_t type,
                status_code_t status,
                const session_id_t *session_id,
                const void *payload,
                uint32_t payload_len)
{
    if (packet == NULL || payload_len > MAX_PACKET_PAYLOAD ||
        (payload_len > 0U && payload == NULL)) {
        return -1;
    }

    memset(packet, 0, sizeof(*packet));
    packet->header.type = type;
    packet->header.status = status;
    packet->header.payload_len = payload_len;
    if (session_id != NULL) {
        packet->header.session_id = *session_id;
    }
    packet->payload = (void *)payload;
    return 0;
}

int protocol_send_header(int fd, const packet_header_t *header)
{
    unsigned char wire_header[TLV_WIRE_HEADER_SIZE];

    if (header == NULL || header->payload_len > MAX_PACKET_PAYLOAD) {
        return -1;
    }

    encode_wire_header(wire_header, header);
    return send_n(fd, wire_header, sizeof(wire_header));
}

int protocol_send_packet(int fd, const packet_t *packet)
{
    if (packet == NULL || packet->header.payload_len > MAX_PACKET_PAYLOAD ||
        (packet->header.payload_len > 0U && packet->payload == NULL)) {
        return -1;
    }

    if (protocol_send_header(fd, &packet->header) != 0) {
        return -1;
    }

    if (packet->header.payload_len > 0U &&
        send_n(fd, packet->payload, packet->header.payload_len) != 0) {
        return -1;
    }
    return 0;
}

int protocol_recv_packet(int fd, packet_t *packet)
{
    unsigned char wire_header[TLV_WIRE_HEADER_SIZE];
    uint32_t magic;
    uint32_t version;

    if (packet == NULL) {
        return -1;
    }

    memset(packet, 0, sizeof(*packet));
    if (recv_n(fd, wire_header, sizeof(wire_header)) != 0) {
        return -1;
    }

    magic = read_net_u32(wire_header + WIRE_MAGIC_OFFSET);
    version = read_net_u32(wire_header + WIRE_VERSION_OFFSET);
    if (magic != TLV_MAGIC || version != TLV_VERSION) {
        return -1;
    }

    packet->header.type =
        (cmd_type_t)read_net_u32(wire_header + WIRE_TYPE_OFFSET);
    packet->header.status =
        (status_code_t)read_net_u32(wire_header + WIRE_STATUS_OFFSET);
    packet->header.payload_len =
        read_net_u32(wire_header + WIRE_PAYLOAD_LEN_OFFSET);
    memcpy(packet->header.session_id.bytes,
           wire_header + WIRE_SESSION_ID_OFFSET,
           SESSION_ID_SIZE);

    if (packet->header.payload_len > MAX_PACKET_PAYLOAD) {
        return -1;
    }
    if (packet->header.payload_len == 0U) {
        return 0;
    }

    packet->payload = malloc(packet->header.payload_len);
    if (packet->payload == NULL) {
        return -1;
    }
    packet->owns_payload = true;

    if (recv_n(fd, packet->payload, packet->header.payload_len) != 0) {
        packet_release(packet);
        return -1;
    }
    return 0;
}

void packet_release(packet_t *packet)
{
    if (packet == NULL) {
        return;
    }
    if (packet->owns_payload) {
        free(packet->payload);
    }
    memset(packet, 0, sizeof(*packet));
}

bool session_id_is_empty(const session_id_t *session_id)
{
    static const session_id_t empty = {{0}};
    return session_id == NULL || session_id_equal(session_id, &empty);
}

bool session_id_equal(const session_id_t *left, const session_id_t *right)
{
    return left != NULL && right != NULL &&
           memcmp(left->bytes, right->bytes, SESSION_ID_SIZE) == 0;
}


int send_puts_resume(int client_fd,
                     const session_id_t *session_id,
                     uint64_t offset)
{
    resume_payload_t resume;
    packet_t packet;

    resume.offset = host_to_net_u64(offset);
    if (packet_init(&packet, CMD_PUTS_RESP, STATUS_OK, session_id,
                    &resume, sizeof(resume)) != 0) {
        return -1;
    }
    return protocol_send_packet(client_fd, &packet);
}
