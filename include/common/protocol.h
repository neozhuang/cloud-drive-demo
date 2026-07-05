#pragma once

#include <limits.h>
#include <linux/limits.h>
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
 * 0x544C5631 is ASCII code TLV1 
 */
#define TLV_MAGIC 0x544C5631U

/*
 * TLV_VERSION:
 * for future updated client/server protocol compatibility 
 */
#define TLV_VERSION 1U

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
 * MAX_COMMAND_ARG:
 * max length that command args have
 */
#define MAX_COMMAND_ARG PATH_MAX

/*
 * MAX_TEXT_PAYLOAD:
 * max length text responded
 */
#define MAX_TEXT_PAYLOAD 4096U

/*
 * FILE_CHUNK_SIZE:
 * the number of bytes that each FILE_DATA packet
 */
#define FILE_CHUNK_SIZE 4096U

/**
 * FILE_OPTIMIZATION_THRESHOLD
 *
 * the large file transfer optimization threshold
 */
#define FILE_OPTIMIZATION_THRESHOLD 1024 * 1024 * 100   // 1M * 100 = 100M

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
} status_code_t;

/*
 * tlv_header_t:
 *
 * - Type   -> cmd_type
 * - Length -> data_len
 * - Value  -> payload
 *
 */
typedef struct {
    uint32_t magic;    
    uint32_t version;  
    uint32_t cmd_type; 
    uint32_t status;   
    uint32_t data_len; // payload length
} tlv_header_t;

typedef struct {
    tlv_header_t header; 
    void *payload; // malloc, if len = 0, payload == NULL
} packet_t;

/*
 * command_request_t:
 * command line parsed result
 *
 * @example
 * mkdir demo 
 * will be parsed as follows:
 *   type = CMD_MKDIR
 *   arg  = "demo"
 */
typedef struct {
    cmd_type_t type; 
    char arg[MAX_COMMAND_ARG];   
} command_request_t;

/*
 * path payload
 */
typedef struct {
    char path[PATH_MAX]; 
} path_payload_t;

/*
 * text_payload_t:
 *
 * for ls/pwd/mkdir/... simple command
 */
typedef struct {
    char text[MAX_TEXT_PAYLOAD];
} text_payload_t;

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

/*
 * resume_payload_t:
 */
typedef struct {
    uint64_t offset;
} resume_payload_t;

/*
 * file_chunk_payload_t:
 */
typedef struct {
    uint32_t data_len;
    unsigned char data[FILE_CHUNK_SIZE];
} file_chunk_payload_t;

/* Interface */

/*
 * str_to_cmd_type
 * parse string to cmd_type_t
 */
cmd_type_t str_to_cmd_type(const char *cmd);

/*
 * cmd_type_to_str
 * parse cmd_type_t to string
 */
const char* cmd_type_to_str(cmd_type_t type);

/*
 * parse_command_request:
 * parse client input to command_request_t variable
 *
 */
int parse_command_request(const char *input, command_request_t *req);

/*
 * build_command_request:
 */
int build_command_request(const char *input, command_request_t *req);

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

int send_packet(int fd, cmd_type_t type, status_code_t status, const void *payload, uint32_t payload_len);

// for sendfile use
int send_packet_header(int fd, cmd_type_t type, status_code_t status, uint32_t payload_len);

int recv_packet(int fd, packet_t *packet);

/*
 * free_packet:
 * free memory that malloced by recv_packet
 */
void free_packet(packet_t *packet);

/*
 * print_text_from_packet:
 * print packet payload as text when the packet contains a text response.
 */
void print_text_from_packet(const packet_t *packet);

int get_text_from_packet(const packet_t* packet, char* text, int size);

int send_puts_resume(int client_fd, uint64_t offset);
