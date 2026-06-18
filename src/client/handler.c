#include "client/handler.h"

#include <fcntl.h>
#include <linux/limits.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>

#include "common/protocol.h"
#include "client/state.h"
#include "common/utils.h"

#define DEFAULT_DOWNLOAD_DIR "downloads"

static int handle_simple_command(client_state_t* client_state, const command_request_t* req);
static int handle_puts(client_state_t* client_state, const command_request_t* req);
static int handle_gets(client_state_t* client_state, const command_request_t* req);

int handle_command_input(client_state_t* client_state, const char *input) 
{
    command_request_t req;
    memset(&req, 0, sizeof(req));

    // parse input to req.type and req.arg
    if (build_command_request(input, &req) != 0) {
        fprintf(stderr, "invalid command\n");
        return -1;
    }

    switch (req.type) {
        case CMD_PWD:
        case CMD_CD:
        case CMD_LS:
            return handle_simple_command(client_state, &req);
        case CMD_MKDIR:
            if (req.arg[0] == '\0') {
                fprintf(stderr, "usage: mkdir <dir>\n");
                return -1;
            }
            return handle_simple_command(client_state, &req);
        case CMD_RMDIR:
            if (req.arg[0] == '\0') {
                fprintf(stderr, "usage: rmdir <dir>\n");
                return -1;
            }
            return handle_simple_command(client_state, &req);
        case CMD_RM:
            if (req.arg[0] == '\0') {
                fprintf(stderr, "usage: mkdir <dir>\n");
                return -1;
            }
            return handle_simple_command(client_state, &req);

        case CMD_PUTS_REQ:
            if (req.arg[0] == '\0') {
                fprintf(stderr, "usage: puts <file>\n");
                return -1;
            }
            return handle_puts(client_state, &req);

        case CMD_GETS_REQ:
            if (req.arg[0] == '\0') {
                fprintf(stderr, "usage: gets <file>\n");
                return -1;
            }
            return handle_gets(client_state, &req);

        default:
            fprintf(stderr, "unsupported currently\n");
            return -1;
    }
}

/*
 * print_text_from_packet:
 * 
 * print packet payload server sended
 */
static void print_text_from_packet(const packet_t *packet) {
    char payload[MAX_PACKET_PAYLOAD];
    size_t len;

    if (packet->payload != NULL && packet->header.data_len > 0U) {
        len = packet->header.data_len;
        if (len >= sizeof(payload)) {
            len = sizeof(payload) - 1;
        }
        memcpy(payload, packet->payload, len);
        payload[len] = '\0';
        printf("%s\n", payload);
    }
}

/*
 * recv_text_response:
 * 
 * recv text response packet
 * the packet will be returned with packet type CMD_ACK or CMD_ERROR
 */
static int recv_text_response(client_state_t* client_state) {
    packet_t packet;
    memset(&packet, 0, sizeof(packet));

    if (recv_packet(client_state->sock_fd, &packet) != 0) {
        fprintf(stderr, "failed to recv server response\n");
        return -1;
    }

    /*
     * if recv packet type is not ACK or ERROR to simple command, 
     * that means the process of protocol is at messy status.
     */
    if ((cmd_type_t)packet.header.cmd_type != CMD_ACK &&
        (cmd_type_t)packet.header.cmd_type != CMD_ERROR) {
        free_packet(&packet);
        fprintf(stderr, "unknown type is returned by server\n");
        return -1;
    }

    // cd success, modify client state cwd
    if ((cmd_type_t)packet.header.cmd_type == CMD_ACK && packet.header.status == STATUS_CD_OK) {
        if (packet.payload != NULL && packet.header.data_len > 0U) {
            size_t len = packet.header.data_len;
            if (len >= sizeof(client_state->remote_cwd)) {
                len = sizeof(client_state->remote_cwd) - 1;
            }
            memcpy(client_state->remote_cwd, packet.payload, len);
            client_state->remote_cwd[len] = '\0';
        }
    }
    print_text_from_packet(&packet);
    free_packet(&packet);
    return 0;
}


static int handle_simple_command(client_state_t* client_state, const command_request_t* req)
{
    if (send_packet(client_state->sock_fd, req->type, STATUS_OK, req->arg, strlen(req->arg)) != 0) {
        fprintf(stderr, "failed to send request\n");
        return -1;
    }

    return recv_text_response(client_state);
}


/*
 * handle_puts:
 *
 * the process of puts:
 *   1. open file + fstat
 *   2. send PUTS_REQ(file_name, file_size)
 *   3. recv PUTS_RESP(phase-02 will return resume_offset)
 *   4. loop: send FILE_DATA
 *   5. send FILE_END
 *   6. recv ACK / ERROR
 */
static int handle_puts(client_state_t* client_state, const command_request_t* req)
{
    int file_fd = -1;
    struct stat st;
    uint64_t file_size;
    char base_name[PATH_MAX];
    file_info_payload_t info;
    file_chunk_payload_t chunk;
    packet_t packet;

    memset(&st, 0, sizeof(st));

    file_fd = open(req->arg, O_RDONLY);
    if (file_fd == -1) {
        perror("open");
        return -1;
    }

    if (fstat(file_fd, &st) != 0) {
        perror("fstat");
        close(file_fd);
        return -1;
    }
    file_size = (uint64_t)st.st_size;

    // protect client dir layout, just send base name to server
    // get base name
    if(get_base_name(req->arg, base_name, sizeof(base_name)) != 0) {
        fprintf(stderr, "failed to get base name from %s\n", req->arg);
        close(file_fd);
        return -1;
    }

    // fill the payload
    memset(&info, 0, sizeof(info));
    strncpy(info.file_name, base_name, strlen(base_name));
    info.file_name[strlen(base_name)] = '\0';
    info.file_size = host_to_net_u64(file_size);

    // 2. send PUTS_REQ(file_name, file_size)
    if (send_packet(client_state->sock_fd, req->type, STATUS_OK, &info, sizeof(info)) != 0) {
        close(file_fd);
        fprintf(stderr, "failed to send puts request\n");
        return -1;
    }

    // 3. recv PUTS_RESP
    memset(&packet, 0, sizeof(packet));
    if(recv_packet(client_state->sock_fd, &packet) != 0) {
        free_packet(&packet);
        close(file_fd);
        fprintf(stderr, "failed to recv puts response\n");
        return -1;
    }

    if (packet.header.cmd_type == CMD_ERROR) {
        print_text_from_packet(&packet);
        free_packet(&packet);
        close(file_fd);
        return -1;
    }
    free_packet(&packet);

    // 4. loop: send FILE_DATA
    uint64_t sended = 0;
    while (sended < file_size) {
        memset(&chunk, 0, sizeof(chunk));
        ssize_t nread = read(file_fd, chunk.data, sizeof(chunk.data));
        if (nread < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(file_fd);
            perror("read");
            return -1;
        }    
        // read eof
        if (nread == 0) {
            break;
        }
        chunk.data_len = htonl(nread);
        if (send_packet(client_state->sock_fd, CMD_FILE_DATA, STATUS_OK, &chunk, sizeof(chunk)) != 0) {
            close(file_fd);
            fprintf(stderr, "failed to send file chunk\n");
            return -1;
        }
        sended += nread;
    }
    close(file_fd);

    // 5. send FILE_END
    if (send_packet(client_state->sock_fd, CMD_FILE_END, STATUS_OK, NULL, 0U) != 0) {
        fprintf(stderr, "failed to send the FILE_END flag\n");
        return -1;
    }

    // 6. recv ack/error
    if (recv_text_response(client_state) != 0) {
        return -1;
    }
    printf("puts completed\n");
    return 0;
}

/*
 * handle_gets:
 *
 * the process of gets:
 * 1. send GETS_REQ(file_name)
 * 2. recv GETS_RESP(file info payload or error)
 * 3. open/create local file
 * 4. loop recv CMD_FILE_DATA
 * 5. recv CMD_FILE_END
 * 6. check recved == file_size
 * 7. send ACK or ERROR
 */
static int handle_gets(client_state_t* client_state, const command_request_t* req)
{
    packet_t packet;
    char downloaded_filename[PATH_MAX];
    char file_name[PATH_MAX]; // The uploaded file name
    int file_fd = -1;
    uint64_t recved = 0;
    uint64_t file_size = 0;
    file_info_payload_t info;
    file_chunk_payload_t chunk;
    unsigned int chunk_size;
    int nwrited;

    // 1. send GETS_REQ(file_name)
    if (send_packet(client_state->sock_fd, req->type, STATUS_OK, req->arg, strlen(req->arg)) != 0) {
        fprintf(stderr, "failed to send gets request\n");
        return -1;
    }

    // 2. recv GETS_RESP(file_size or error)
    memset(&packet, 0, sizeof(packet));
    if (recv_packet(client_state->sock_fd, &packet) != 0) {
        fprintf(stderr, "failed to recv server resp\n");
        free_packet(&packet);
        return -1;
    }

    if (packet.header.cmd_type == CMD_ERROR) {
        if (packet.header.status == STATUS_NOT_FOUND) {
            fprintf(stderr, "file %s not exist\n", req->arg);
        }
        free_packet(&packet);
        return -1;
    }
    
    // get the uploaded file name and file size
    memset(&info, 0, sizeof(info));
    memcpy(&info, packet.payload, packet.header.data_len);
    file_size = net_to_host_u64(info.file_size);
    memcpy(file_name, info.file_name, strlen(info.file_name));
    file_name[strlen(info.file_name)] = '\0';

    free_packet(&packet);

    // 3. open/create local file
    // concat the path of downloads and file name
    strcat(downloaded_filename, DEFAULT_DOWNLOAD_DIR);
    strcat(downloaded_filename, "/");
    strcat(downloaded_filename, file_name);
    // mkdir downloads/ if not exist
    if (mkdir_p(DEFAULT_DOWNLOAD_DIR, 0755) != 0) {
        return -1;
    }
    file_fd = open(downloaded_filename, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (file_fd == -1) {
        perror("open");
        return -1;
    }
    // 4. loop recv CMD_FILE_DATA
    // recv file data
    int got_file_end = 0;   // file end flag
    while (recved < file_size) {
        memset(&packet, 0, sizeof(packet));

        if (recv_packet(client_state->sock_fd, &packet) != 0) {
            send_packet(client_state->sock_fd, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
            close(file_fd);
            free_packet(&packet);
            return -1;
        }

        if (packet.header.cmd_type == CMD_FILE_END) {
            got_file_end = 1;
            free_packet(&packet);
            break;
        }

        if (packet.header.cmd_type != CMD_FILE_DATA) {
            free_packet(&packet);
            send_packet(client_state->sock_fd, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
            close(file_fd);
            return -1;
        }

        memcpy(&chunk, packet.payload, sizeof(chunk));

        chunk_size = ntohl(chunk.data_len);
        if (chunk_size == 0 || chunk_size > FILE_BLOCK_SIZE) {
            free_packet(&packet);
            send_packet(client_state->sock_fd, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
            close(file_fd);
            return -1;
        }

        nwrited = write(file_fd, chunk.data, chunk_size);
        if (nwrited != (ssize_t)chunk_size) {
            free_packet(&packet);
            send_packet(client_state->sock_fd, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
            close(file_fd);
            return -1;
        }

        recved += chunk_size;
        free_packet(&packet);
    }

    //  6. check recved == file_size
    if (recved == file_size) {
        if (!got_file_end) {
            if (recv_packet(client_state->sock_fd, &packet) != 0) {
                send_packet(client_state->sock_fd, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
                close(file_fd);
                return -1;
            }

            if (packet.header.cmd_type != CMD_FILE_END) {
                free_packet(&packet);
                send_packet(client_state->sock_fd, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
                close(file_fd);
                return -1;
            }
            free_packet(&packet);
        }

        // 7. send ACK or ERROR
        close(file_fd);
        send_packet(client_state->sock_fd, CMD_ACK, STATUS_OK, NULL, 0);
        printf("gets file %s completed\n", file_name);
    } else {
        close(file_fd);
        send_packet(client_state->sock_fd, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
    }

    return 0;
}
