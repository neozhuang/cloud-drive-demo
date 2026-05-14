#include "protocol/protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

int protocol_is_supported_packet_type(uint16_t type)
{
    switch (type) {
    case PROTOCOL_PACKET_PING:
    case PROTOCOL_PACKET_QUIT:
    case PROTOCOL_PACKET_REGISTER_REQUEST:
    case PROTOCOL_PACKET_REGISTER:
    case PROTOCOL_PACKET_LOGIN_REQUEST:
    case PROTOCOL_PACKET_LOGIN:
    case PROTOCOL_PACKET_DELETE_REQUEST:
    case PROTOCOL_PACKET_DELETE:
    case PROTOCOL_PACKET_LOGOUT:
    case PROTOCOL_PACKET_PWD:
    case PROTOCOL_PACKET_CD:
    case PROTOCOL_PACKET_LS:
    case PROTOCOL_PACKET_LL:
    case PROTOCOL_PACKET_TREE:
    case PROTOCOL_PACKET_MKDIR:
    case PROTOCOL_PACKET_RMDIR:
    case PROTOCOL_PACKET_RM:
    case PROTOCOL_PACKET_CAT:
    case PROTOCOL_PACKET_UPLOAD:
    case PROTOCOL_PACKET_DOWNLOAD:
        return 1;
    default:
        return 0;
    }
}

const char *protocol_packet_type_name(uint16_t type)
{
    switch (type) {
    case PROTOCOL_PACKET_PING:
        return "PING";
    case PROTOCOL_PACKET_QUIT:
        return "QUIT";
    case PROTOCOL_PACKET_REGISTER_REQUEST:
        return "REGISTER_REQUEST";
    case PROTOCOL_PACKET_REGISTER:
        return "REGISTER";
    case PROTOCOL_PACKET_LOGIN_REQUEST:
        return "LOGIN_REQUEST";
    case PROTOCOL_PACKET_LOGIN:
        return "LOGIN";
    case PROTOCOL_PACKET_DELETE_REQUEST:
        return "DELETE_REQUEST";
    case PROTOCOL_PACKET_DELETE:
        return "DELETE";
    case PROTOCOL_PACKET_LOGOUT:
        return "LOGOUT";
    case PROTOCOL_PACKET_PWD:
        return "PWD";
    case PROTOCOL_PACKET_CD:
        return "CD";
    case PROTOCOL_PACKET_LS:
        return "LS";
    case PROTOCOL_PACKET_LL:
        return "LL";
    case PROTOCOL_PACKET_TREE:
        return "TREE";
    case PROTOCOL_PACKET_MKDIR:
        return "MKDIR";
    case PROTOCOL_PACKET_RMDIR:
        return "RMDIR";
    case PROTOCOL_PACKET_RM:
        return "RM";
    case PROTOCOL_PACKET_CAT:
        return "CAT";
    case PROTOCOL_PACKET_UPLOAD:
        return "UPLOAD";
    case PROTOCOL_PACKET_DOWNLOAD:
        return "DOWNLOAD";
    default:
        return "INVALID";
    }
}

const char *protocol_status_name(uint16_t status)
{
    switch (status) {
    case PROTOCOL_STATUS_OK:
        return "OK";
    case PROTOCOL_STATUS_USERNAME_ALREADY_EXISTS:
        return "USERNAME_ALREADY_EXISTS";
    case PROTOCOL_STATUS_USERNAME_NOT_EXIST:
        return "USERNAME_NOT_EXIST";
    case PROTOCOL_STATUS_PASSWORD_INCORRECT:
        return "PASSWORD_INCORRECT";
    case PROTOCOL_STATUS_INTERNAL_ERROR:
        return "INTERNAL_ERROR";
    default:
        return "UNKNOWN_STATUS";
    }
}
