#ifndef CLOUD_DRIVE_PROTOCOL_H
#define CLOUD_DRIVE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define PROTOCOL_HEADER_SIZE 8u
#define PROTOCOL_MAX_PAYLOAD_SIZE 4096u
#define PROTOCOL_MAX_PACKET_SIZE (PROTOCOL_HEADER_SIZE + PROTOCOL_MAX_PAYLOAD_SIZE)

typedef enum ProtocolPacketType {
    PROTOCOL_PACKET_INVALID = 0,

    /* Basic control packets. */
    PROTOCOL_PACKET_PING = 1,
    PROTOCOL_PACKET_QUIT = 2,

    /* User account packets. */
    PROTOCOL_PACKET_REGISTER_REQUEST = 100,
    PROTOCOL_PACKET_REGISTER = 101,
    PROTOCOL_PACKET_LOGIN_REQUEST = 102,
    PROTOCOL_PACKET_LOGIN = 103,
    PROTOCOL_PACKET_DELETE_REQUEST = 104,
    PROTOCOL_PACKET_DELETE = 105,
    PROTOCOL_PACKET_LOGOUT = 106,

    /* Directory and file operation packets. */
    PROTOCOL_PACKET_PWD = 200,
    PROTOCOL_PACKET_CD = 201,
    PROTOCOL_PACKET_LS = 202,
    PROTOCOL_PACKET_LL = 203,
    PROTOCOL_PACKET_TREE = 204,
    PROTOCOL_PACKET_MKDIR = 205,
    PROTOCOL_PACKET_RMDIR = 206,
    PROTOCOL_PACKET_RM = 207,
    PROTOCOL_PACKET_CAT = 208,

    /* File transfer packets. */
    PROTOCOL_PACKET_UPLOAD = 300,
    PROTOCOL_PACKET_DOWNLOAD = 301
} ProtocolPacketType;

typedef enum ProtocolStatus {
    PROTOCOL_STATUS_OK = 0,
    //
    PROTOCOL_STATUS_USERNAME_ALREADY_EXISTS = 1000,
    PROTOCOL_STATUS_USERNAME_NOT_EXIST = 1001,
    PROTOCOL_STATUS_PASSWORD_INCORRECT = 1002,
    //
    PROTOCOL_STATUS_INTERNAL_ERROR = 3000
} ProtocolStatus;

typedef struct ProtocolPacket {
    uint16_t type;
    uint16_t status;
    uint32_t payload_length;
    unsigned char payload[PROTOCOL_MAX_PAYLOAD_SIZE];
} ProtocolPacket;

int protocol_is_supported_packet_type(uint16_t type);
const char *protocol_packet_type_name(uint16_t type);
const char *protocol_status_name(uint16_t status);

#endif
