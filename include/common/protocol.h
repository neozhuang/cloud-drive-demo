#pragma once

#include <limits.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 *  Platform compatibility 
 */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/*
 * TLV_MAGIC:
 * ensure data received from our own defined protocol
 * 0x544C5632 is ASCII code TLV2
 */
#define TLV_MAGIC 0x544C5632U

/*
 * TLV_VERSION:
 * for future updated client/server protocol compatibility 
 */
#define TLV_VERSION 2U

#define SESSION_ID_SIZE 16U

/*
 * Wire format:
 * magic      4 bytes
 * version    4 bytes
 * cmd_type   4 bytes
 * status     4 bytes
 * data_len   4 bytes
 * session_id 16 bytes
 */
#define TLV_WIRE_HEADER_SIZE (20U + SESSION_ID_SIZE)

/*
 * MAX_PACKET_PAYLOAD:
 * max payload each packet contained 
 */
#define MAX_PACKET_PAYLOAD (64U * 1024U)

/*
 * MAX_COMMAND_INPUT:
 * max command input length
 */
#define MAX_COMMAND_INPUT 512U

/*
 * FILE_CHUNK_SIZE:
 * the number of bytes that each FILE_DATA packet
 */
#define FILE_CHUNK_SIZE 4096U

typedef enum {
    CMD_INVALID = 0,        // invalid command
    CMD_LOGIN_REQ,          // user login request
    CMD_REGISTER_REQ,       // user register request
    CMD_PWD,         
    CMD_CD,          
    CMD_LS,          
    CMD_LL,
    CMD_TREE,
    CMD_MKDIR,       
    CMD_RMDIR,
    CMD_RM,          
    CMD_CAT,
    CMD_PUTS_REQ,    
    CMD_PUTS_RESP,   /* puts response：server return offset */
    CMD_GETS_REQ,
    CMD_GETS_RESP,   /* gets response：server return file meta info */
    CMD_RESUME_POS,  /* offset position */
    CMD_FILE_DATA,   /* file data packet */
    CMD_FILE_END,    /* file end packet */
    CMD_ACK,        // general success response
    CMD_ERROR       // general error response
} cmd_type_t;

/*
 * status_code_t:
 */
typedef enum {
    STATUS_OK = 0,          // success
    STATUS_BAD_REQUEST = 2,   /* 请求格式错误、参数错误 */
    STATUS_NOT_FOUND = 3,     /* 文件或目录不存在 */
    STATUS_IO_ERROR = 4,      /* 本地 IO 错误，例如 open/mkdir/remove 失败 */
    STATUS_PROTOCOL_ERROR = 5, /* 协议流程或包格式错误 */
    STATUS_USER_NOTEXIST,       // user not exist
    STATUS_PASSWD_ERROR,        // password error
    STATUS_OTHER_ERROR,
    STATUS_NOT_DIR,             // cd, not dir
    STATUS_IS_DIR,              // rm, is dir
    STATUS_DIR_NOTEXIST,         // cd, dir not exist
    STATUS_DIR_ALREADY_EXISTS,   // mkdir, dir already exists
    STATUS_DIR_NOTEMPTY,         // rmdir, dir not empty
    STATUS_FILE_NOTEXIST,        // rm, file not exist

    STATUS_DB_ERROR,
    STATUS_UNAUTHORIZED,
} status_code_t;



typedef struct {
    unsigned char bytes[SESSION_ID_SIZE];
} session_id_t;

typedef struct {
    cmd_type_t type;
    status_code_t status;
    uint32_t payload_len;
    session_id_t session_id;
} packet_header_t;

typedef struct {
    packet_header_t header;
    void *payload;
    bool owns_payload; /* true if packet_t is responsible for freeing payload */
} packet_t;

/*
 * file_info_payload_t:
 *
 * for puts/gets
 */
typedef struct {
    char file_name[NAME_MAX];
    uint64_t file_size;
    char sha256_hex[65];
} file_info_payload_t;

typedef struct {
    char remote_path[PATH_MAX];
    uint64_t file_size;
    char sha256_hex[65];
} upload_request_payload_t;

/*
 * resume_payload_t:
 */
typedef struct {
    uint64_t offset;
} resume_payload_t;

/* Interface */

/*
 * cmd_type_to_str
 * parse cmd_type_t to string
 */
const char* cmd_type_to_str(cmd_type_t type);

/*
 * host_to_net_u64 / net_to_host_u64:
 * transfer 64 int to and from between host and net bytes order
 *
 * why do it ourselves?
 * htonl/ntohl use uint32_t
 */
uint64_t host_to_net_u64(uint64_t value);
uint64_t net_to_host_u64(uint64_t value);

int send_n(int fd, const void *buf, size_t len);
int recv_n(int fd, void *buf, size_t len);

int packet_init(packet_t *packet,
                cmd_type_t type,
                status_code_t status,
                const session_id_t *session_id,
                const void *payload,
                uint32_t payload_len);

int protocol_send_packet(int fd, const packet_t *packet);
int protocol_recv_packet(int fd, packet_t *packet);
/* Send only a frame header before a zero-copy payload such as sendfile(). */
int protocol_send_header(int fd, const packet_header_t *header);
void packet_release(packet_t *packet);

bool session_id_is_empty(const session_id_t *session_id);
bool session_id_equal(const session_id_t *left, const session_id_t *right);

int send_puts_resume(int client_fd,
                     const session_id_t *session_id,
                     uint64_t offset);
