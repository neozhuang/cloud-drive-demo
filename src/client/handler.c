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
#include <sys/mman.h>

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
 * add feat: resumable transfer and large file transfer optimization
 *
 * the process of puts:
 *   1. open file + fstat
 *   2. send PUTS_REQ(file_name, file_size)
 *   3. recv PUTS_RESP(resume_offset or error)
 *   4. lseek the position
 *   5. small file: read, large file: mmap
 *   6. loop: send FILE_DATA
 *   7. send FILE_END
 *   8. recv ACK / ERROR
 */
static int handle_puts(client_state_t* client_state, const command_request_t* req)
{
    int file_fd = -1;
    struct stat st;
    uint64_t file_size;
    uint64_t file_offset;
    char base_name[PATH_MAX];
    file_info_payload_t info;
    packet_t packet;

    // 1. open file + fstat
    file_fd = open(req->arg, O_RDONLY);
    if (file_fd == -1) {
        perror("open");
        return -1;
    }

    memset(&st, 0, sizeof(st));
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
    snprintf(info.file_name, sizeof(info.file_name), "%s", base_name);
    info.file_size = host_to_net_u64(file_size);

    // 2. send PUTS_REQ(file_name, file_size)
    if (send_packet(client_state->sock_fd, req->type, STATUS_OK, &info, sizeof(info)) != 0) {
        close(file_fd);
        fprintf(stderr, "failed to send puts request\n");
        return -1;
    }

    // 3. recv PUTS_RESP(resume_offset or error)
    memset(&packet, 0, sizeof(packet));
    if(recv_packet(client_state->sock_fd, &packet) != 0) {
        free_packet(&packet);
        close(file_fd);
        fprintf(stderr, "failed to recv puts response\n");
        return -1;
    }

    if (packet.header.cmd_type != CMD_PUTS_RESP) {
        print_text_from_packet(&packet);
        free_packet(&packet);
        close(file_fd);
        return -1;
    }

    resume_payload_t resume;
    memcpy(&resume, packet.payload, sizeof(resume));
    file_offset = net_to_host_u64(resume.offset);
    free_packet(&packet);

    // 4. lseek the position
    if (lseek(file_fd, file_offset, SEEK_SET) == -1) {
        close(file_fd);
        send_packet(client_state->sock_fd, CMD_ERROR, STATUS_IO_ERROR, NULL, 0); 
        perror("lseek");
        return -1;
    }

    if (file_offset > 0) {
        printf("resume puts from position %lu\n", file_offset);
    }

    // 5. small file: read, large file: mmap
    if (file_size - file_offset > FILE_OPTIMIZATION_THRESHOLD) {
        // get page size
        long page_size = sysconf(_SC_PAGE_SIZE);
        if (page_size == -1) {
            close(file_fd);
            perror("sysconf");
            return -1;
        }

        uint64_t map_offset = file_offset / page_size * page_size;
        uint64_t offset_delta = file_offset - map_offset;
        uint64_t map_len = file_size - map_offset;

        char* map = mmap(NULL, map_len, PROT_READ, MAP_SHARED, file_fd, map_offset);
        if (map == MAP_FAILED) {
            close(file_fd);
            perror("mmap");
            return -1;
        }

        char* p = map + offset_delta;   // the file position that start to send
        uint64_t remaining = file_size - file_offset; 

        // 6. loop: send FILE_DATA
        while (remaining > 0) {
            uint32_t nsend = remaining > FILE_CHUNK_SIZE ? FILE_CHUNK_SIZE : remaining;
            if (send_packet(client_state->sock_fd, CMD_FILE_DATA, STATUS_OK, p, nsend) != 0) {
                munmap(map, map_len);
                close(file_fd);
                fprintf(stderr, "failed to send file chunk\n");
                return -1;
            }
            p += nsend;
            remaining -= nsend;
        }
        munmap(map, map_len);

    }else {
        // small file just use read and send
        uint64_t remaining = file_size - file_offset; 
        char buffer[FILE_CHUNK_SIZE];
        // 6. loop: send FILE_DATA
        while (remaining > 0) {
            ssize_t nread = read(file_fd, buffer, sizeof(buffer)); 
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
            if (send_packet(client_state->sock_fd, CMD_FILE_DATA, STATUS_OK, buffer, nread) != 0) {
                close(file_fd);
                fprintf(stderr, "failed to send file chunk\n");
                return -1;
            }
            remaining -= nread;
        }
    }

    close(file_fd);

    // 7. send FILE_END
    if (send_packet(client_state->sock_fd, CMD_FILE_END, STATUS_OK, NULL, 0U) != 0) {
        fprintf(stderr, "failed to send the FILE_END flag\n");
        return -1;
    }

    // 8. recv ACK / ERROR
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
 * 3. stat, send cmd_resume_pos
 * 4. open file, lseek
 * 5. loop recv CMD_FILE_DATA
 * 6. recv CMD_FILE_END
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
    unsigned int chunk_size;
    resume_payload_t resume;
    int nwrited;
    struct stat st;
    uint64_t offset = 0;

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

    // 3. stat, send cmd_resume_pos
    // concat the path of downloads and file name
    strcat(downloaded_filename, DEFAULT_DOWNLOAD_DIR);
    strcat(downloaded_filename, "/");
    strcat(downloaded_filename, file_name);
    mkdir_p(DEFAULT_DOWNLOAD_DIR, 0755);
    if (stat(downloaded_filename, &st)  == -1) {
        if (errno == ENOENT) {
            offset = 0; // local file is not exist
        } else {
            perror("stat");
            return -1;
        }
    } else {
        offset = (uint64_t)st.st_size;
    }
    resume.offset = host_to_net_u64(offset);
    send_packet(client_state->sock_fd, CMD_RESUME_POS, STATUS_OK, &resume, sizeof(resume));

    if (offset > 0) {
        printf("resume gets from position %lu\n", offset);
    }

    // 4. open file, lseek
    file_fd = open(downloaded_filename, O_CREAT | O_WRONLY | O_APPEND, 0666);
    if (file_fd == -1) {
        perror("open");
        return -1;
    }

    // 5. loop recv CMD_FILE_DATA
    // recv file data
    int got_file_end = 0;   // file end flag
    while (recved < file_size - offset) {
        memset(&packet, 0, sizeof(packet));

        if (recv_packet(client_state->sock_fd, &packet) != 0) {
            send_packet(client_state->sock_fd, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
            close(file_fd);
            free_packet(&packet);
            return -1;
        }

        // 6. recv CMD_FILE_END
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

        chunk_size = packet.header.data_len; 
        if (chunk_size == 0 || chunk_size > FILE_CHUNK_SIZE) {
            free_packet(&packet);
            send_packet(client_state->sock_fd, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
            close(file_fd);
            return -1;
        }

        nwrited = write(file_fd, packet.payload, chunk_size);
        if (nwrited != (ssize_t)chunk_size) {
            free_packet(&packet);
            send_packet(client_state->sock_fd, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
            close(file_fd);
            return -1;
        }

        recved += chunk_size;
        free_packet(&packet);
    }

    if (recved == file_size - offset) {
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
