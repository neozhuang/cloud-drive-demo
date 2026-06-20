#include "common/protocol.h"

#include <string.h>
#include <sys/socket.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * str_to_cmd_type
 * parse string to cmd_type_t
 */
cmd_type_t str_to_cmd_type(const char *cmd)
{
    if (cmd == NULL || *cmd == '\0') {
        return CMD_INVALID;
    }
    if (strcmp(cmd, "pwd") == 0) {
        return CMD_PWD;
    }
    if (strcmp(cmd, "cd") == 0) {
        return CMD_CD;
    }
    if (strcmp(cmd, "ls") == 0) {
        return CMD_LS;
    }
    if (strcmp(cmd, "rm") == 0 || strcmp(cmd, "remove") == 0) {
        return CMD_RM;
    }
    if (strcmp(cmd, "mkdir") == 0) {
        return CMD_MKDIR;
    }
    if (strcmp(cmd, "rmdir") == 0) {
        return CMD_RMDIR;
    }
    if (strcmp(cmd, "puts") == 0) {
        return CMD_PUTS_REQ;
    }
    if (strcmp(cmd, "gets") == 0) {
        return CMD_GETS_REQ;
    }

    return CMD_INVALID;
}

/*
 * cmd_type_to_str
 * parse cmd_type_t to string
 */
const char* cmd_type_to_str(cmd_type_t type)
{
    switch (type) {
        case CMD_LOGIN_REQ: return "login";
        case CMD_PWD:       return "pwd";
        case CMD_CD:        return "cd";
        case CMD_LS:        return "ls";
        case CMD_RM:        return "rm";
        case CMD_MKDIR:     return "mkdir";
        case CMD_RMDIR:     return "rmdir";
        case CMD_PUTS_REQ:  return "puts";
        case CMD_GETS_REQ:  return "gets";
        case CMD_PUTS_RESP: return "puts_resp";
        case CMD_GETS_RESP: return "gets_resp";
        case CMD_RESUME_POS:return "resume_pos";
        case CMD_FILE_DATA: return "file_data";
        case CMD_FILE_END:  return "file_end";
        case CMD_ACK:       return "ack";
        case CMD_ERROR:     return "error";
        case CMD_INVALID:
        default:
            return "invalid";
    }
}


/*
 * host_to_net_u64 / net_to_host_u64:
 * transfer 64 int to and from between host and net bytes order
 *
 * why do it ourselves?
 * htonl/ntohl use uint32_t
 */
uint64_t host_to_net_u64(uint64_t value)
{
    uint32_t high = htonl((uint32_t)(value >> 32));
    uint32_t low = htonl((uint32_t)(value & 0xffffffffU));

    /*
     * caution:
     * net byte order:
     */
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

int send_packet(int fd, cmd_type_t type, status_code_t status, const void *payload, uint32_t payload_len)
{
    tlv_header_t header = {
        .magic = htonl(TLV_MAGIC),
        .version = htonl(TLV_VERSION),
        .cmd_type = htonl((uint32_t)type),
        .status = htonl((uint32_t)status),
        .data_len = htonl(payload_len)
    };

    // defensive check
    if (payload_len > MAX_PACKET_PAYLOAD) {
        return -1;
    }

    // send packet header
    if (send_n(fd, &header, sizeof(header)) != 0) {
        return -1;
    }

    // only payload_len > 0 and payload is not null 
    if (payload_len > 0 && payload != NULL) {
        if (send_n(fd, payload, payload_len) != 0) {
            return -1;
        }
    }

    return 0;
}

int send_packet_header(int fd, cmd_type_t type, status_code_t status, uint32_t payload_len)
{
    tlv_header_t header = {
        .magic = htonl(TLV_MAGIC),
        .version = htonl(TLV_VERSION),
        .cmd_type = htonl((uint32_t)type),
        .status = htonl((uint32_t)status),
        .data_len = htonl(payload_len)
    };

    // defensive check
    if (payload_len > MAX_PACKET_PAYLOAD) {
        return -1;
    }

    // send packet header
    if (send_n(fd, &header, sizeof(header)) != 0) {
        return -1;
    }
    return 0;
}

int recv_packet(int fd, packet_t *packet)
{
    /*
     * unpack packet order:
     * 1. read fixed header
     * 2. translate header field from net byte order to host byte order
     * 3. check magic / version / data_len
     * 4. if exists payload, then malloc to read it
     */
    tlv_header_t wire_header;
    uint32_t payload_len;

    if (packet == NULL) {
        return -1;
    }

    memset(packet, 0, sizeof(*packet));

    // first read 20 bytes protocol header
    if (recv_n(fd, &wire_header, sizeof(wire_header)) != 0) {
        return -1;
    }

    // recved data is net byte order, translate it to host byte order
    packet->header.magic = ntohl(wire_header.magic);
    packet->header.version = ntohl(wire_header.version);
    packet->header.cmd_type = ntohl(wire_header.cmd_type);
    packet->header.status = ntohl(wire_header.status);
    packet->header.data_len = ntohl(wire_header.data_len);

    // check magic and version, if abnormal, then the data packet may be not ours
    if (packet->header.magic != TLV_MAGIC || packet->header.version != TLV_VERSION) {
        return -1;
    }

    payload_len = packet->header.data_len;

    // limit packet length to avoid bad guy send super packet malicously.
    if (payload_len > MAX_PACKET_PAYLOAD) {
        return -1;
    }

    // no payload
    if (payload_len == 0) {
        return 0;
    }

    // malloc memory for payload
    packet->payload = calloc(1, payload_len);
    if (packet->payload == NULL) {
        return -1;
    }

    if (recv_n(fd, packet->payload, payload_len) != 0) {
        free_packet(packet);
        return -1;
    }

    return 0;
}

/*
 * free_packet:
 * free payload memory that malloced by recv_packet
 * make the header be zero to invaild the packet
 */
void free_packet(packet_t *packet)
{
    if (packet == NULL) {
        return;
    }

    free(packet->payload);
    packet->payload = NULL;
    memset(&packet->header, 0, sizeof(packet->header));
}

/*
 * fill_request:
 * put cmd_type_t and arg to command_request_t safely
 */
static int fill_request(cmd_type_t type, const char *arg, command_request_t *req) {
    if (req == NULL) {
        return -1;
    }

    memset(req, 0, sizeof(*req));

    req->type = type;

    // arg can be null, e.x., pwd, ls
    if (arg != NULL) {
        strncpy(req->arg, arg, sizeof(req->arg));
        req->arg[sizeof(req->arg) - 1] = '\0';
    }
    return 0;
}

/*
 * parse_command_request:
 * parse command line to cmd + arg
 * example：
 *   "mkdir demo"
 * will be parsed：
 *   cmd = "mkdir"
 *   arg = "demo"
 */
int parse_command_request(const char *input, command_request_t *req) {
    char cmd[64] = {0};
    char arg[MAX_COMMAND_ARG] = {0};
    cmd_type_t type;

    if (input == NULL || req == NULL) {
        return -1;
    }

    /*
     * %63s       -> read most 63 not-blink characters to cmd
     * %4095[^\n] -> read all characters until '\n' to arg
     */
    if (sscanf(input, "%63s %4095[^\n]", cmd, arg) < 1) {
        return -1;
    }

    type = str_to_cmd_type(cmd);
    if (type == CMD_INVALID) {
        return -1;
    }

    return fill_request(type, arg, req);
}

/*
 * this is shallow decoration version of parse_command_request 
 * for future use
 */
int build_command_request(const char *input, command_request_t *req) {
    return parse_command_request(input, req);
}
