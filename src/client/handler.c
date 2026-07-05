#include "client/handler.h"

#include <fcntl.h>
#include <linux/limits.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/mman.h>

#include "common/log.h"
#include "common/protocol.h"
#include "client/state.h"
#include "common/utils.h"

#define DEFAULT_DOWNLOAD_DIR "downloads"

static void handle_pwd(client_state_t* client_state, const command_request_t* req);
static void handle_cd(client_state_t* client_state, const command_request_t* req);
static void handle_ls(client_state_t* client_state, const command_request_t* req);
static void handle_mkdir(client_state_t* client_state, const command_request_t* req);
static void handle_rmdir(client_state_t* client_state, const command_request_t* req);
static void handle_rm(client_state_t* client_state, const command_request_t* req);

static void handle_puts(client_state_t* client_state, const command_request_t* req);
static void handle_gets(client_state_t* client_state, const command_request_t* req);

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
            handle_pwd(client_state, &req); break;
        case CMD_CD:
            handle_cd(client_state, &req); break;
        case CMD_LS:
            handle_ls(client_state, &req); break;
        case CMD_LL:
            // handle_ll(client_state, &req); 
            fprintf(stderr, "%s command unsupported currently\n",
                    cmd_type_to_str(req.type));
            break;
        case CMD_TREE:
            // handle_tree(client_state, &req); 
            fprintf(stderr, "%s command unsupported currently\n",
                    cmd_type_to_str(req.type));
            break;
        case CMD_MKDIR:
            if (req.arg[0] == '\0') {
                fprintf(stderr, "usage: mkdir <dir>\n");
                return -1;
            }
            handle_mkdir(client_state, &req); break;
        case CMD_RMDIR:
            if (req.arg[0] == '\0') {
                fprintf(stderr, "usage: rmdir <dir>\n");
                return -1;
            }
            handle_rmdir(client_state, &req); break;
        case CMD_RM:
            if (req.arg[0] == '\0') {
                fprintf(stderr, "usage: rm <file>\n");
                return -1;
            }
            handle_rm(client_state, &req); break;
        case CMD_CAT:
            if (req.arg[0] == '\0') {
                fprintf(stderr, "usage: cat <file>\n");
                return -1;
            }
            // handle_cat(client_state, &req); 
            fprintf(stderr, "%s command unsupported currently\n",
                    cmd_type_to_str(req.type));
            break;
        case CMD_PUTS_REQ:
            if (req.arg[0] == '\0') {
                fprintf(stderr, "usage: puts <file>\n");
                return -1;
            }
            handle_puts(client_state, &req); break;

        case CMD_GETS_REQ:
            if (req.arg[0] == '\0') {
                fprintf(stderr, "usage: gets <file>\n");
                return -1;
            }
            handle_gets(client_state, &req); break;

        default:
            fprintf(stderr, "%s command unsupported currently\n",
                    cmd_type_to_str(req.type));
            return -1;
    }
    return 0;
}

static void handle_pwd(client_state_t* client_state, const command_request_t* req)
{
    send_packet(client_state->sock_fd, req->type, STATUS_OK, req->arg, strlen(req->arg));

    packet_t packet;
    memset(&packet, 0, sizeof(packet));

    recv_packet(client_state->sock_fd, &packet);
    cmd_type_t cmd_type = packet.header.cmd_type;

    if (cmd_type != CMD_ACK && cmd_type != CMD_ERROR) {
        free_packet(&packet);
        LOG_ERROR("Unknown type is returned by server");
    }

    print_text_from_packet(&packet);
    free_packet(&packet);
    LOG_INFO("pwd success");
}

static void handle_cd(client_state_t* client_state, const command_request_t* req)
{
    send_packet(client_state->sock_fd, req->type, STATUS_OK, req->arg, strlen(req->arg));

    packet_t packet;
    char text_resp[MAX_PACKET_PAYLOAD];
    memset(&packet, 0, sizeof(packet));

    recv_packet(client_state->sock_fd, &packet);
    cmd_type_t cmd_type = packet.header.cmd_type;
    get_text_from_packet(&packet, text_resp, sizeof(text_resp));

    if (cmd_type != CMD_ACK && cmd_type != CMD_ERROR) {
        free_packet(&packet);
        LOG_ERROR("Unknown type is returned by server");
    }

    if (cmd_type == CMD_ERROR) {
        if (packet.header.status == STATUS_DIR_NOTEXIST) {
            printf("Directory %s not exists\n", text_resp);
        } else if (packet.header.status == STATUS_NOT_DIR) {
            printf("%s is not a directory\n", text_resp);
        } else {
            printf("other error\n");
        }
    }

    // cd success, modify client state cwd
    if (cmd_type == CMD_ACK) {
        strncpy(client_state->remote_cwd, text_resp, strlen(text_resp));
        client_state->remote_cwd[strlen(text_resp)] = '\0';
        LOG_INFO("cd %s success", text_resp);
    }
    free_packet(&packet);
}

static void handle_ls(client_state_t* client_state, const command_request_t* req)
{
    send_packet(client_state->sock_fd, req->type, STATUS_OK, req->arg, strlen(req->arg));

    packet_t packet;
    char text_resp[MAX_PACKET_PAYLOAD];
    memset(&packet, 0, sizeof(packet));

    recv_packet(client_state->sock_fd, &packet);
    cmd_type_t cmd_type = packet.header.cmd_type;
    get_text_from_packet(&packet, text_resp, sizeof(text_resp));

    if (cmd_type != CMD_ACK && cmd_type != CMD_ERROR) {
        free_packet(&packet);
        LOG_ERROR("Unknown type is returned by server");
    }

    if (cmd_type == CMD_ERROR) {
        if (packet.header.status == STATUS_DIR_NOTEXIST) {
            printf("'%s': No such directory or file\n", req->arg);
        } else {
            printf("other error\n");
        }
    }

    // ls success
    if (cmd_type == CMD_ACK) {
        print_text_from_packet(&packet);
        LOG_INFO("ls %s success", req->arg);
    }

    free_packet(&packet);
}

static void handle_mkdir(client_state_t* client_state, const command_request_t* req)
{
    send_packet(client_state->sock_fd, req->type, STATUS_OK, req->arg, strlen(req->arg));

    packet_t packet;
    char text_resp[MAX_PACKET_PAYLOAD];
    memset(&packet, 0, sizeof(packet));

    recv_packet(client_state->sock_fd, &packet);
    cmd_type_t cmd_type = packet.header.cmd_type;
    get_text_from_packet(&packet, text_resp, sizeof(text_resp));

    if (cmd_type != CMD_ACK && cmd_type != CMD_ERROR) {
        free_packet(&packet);
        LOG_ERROR("Unknown type is returned by server");
    }

    if (cmd_type == CMD_ERROR) {
        if (packet.header.status == STATUS_DIR_ALREADY_EXISTS) {
            printf("Directory %s has already exists\n", text_resp);
        } else if (packet.header.status == STATUS_NOT_DIR) {
            printf("%s is not a directory\n", text_resp);
        } else if (packet.header.status == STATUS_DIR_NOTEXIST) {
            printf("Directory %s not exist\n", text_resp);
        } else {
            printf("other error\n");
        }
    }

    // mkdir success
    if (cmd_type == CMD_ACK) {
        LOG_INFO("mkdir %s success", text_resp);
    }

    free_packet(&packet);
}

static void handle_rmdir(client_state_t* client_state, const command_request_t* req)
{
    send_packet(client_state->sock_fd, req->type, STATUS_OK, req->arg, strlen(req->arg));

    packet_t packet;
    char text_resp[MAX_PACKET_PAYLOAD];
    memset(&packet, 0, sizeof(packet));

    recv_packet(client_state->sock_fd, &packet);
    cmd_type_t cmd_type = packet.header.cmd_type;
    get_text_from_packet(&packet, text_resp, sizeof(text_resp));

    if (cmd_type != CMD_ACK && cmd_type != CMD_ERROR) {
        free_packet(&packet);
        LOG_ERROR("Unknown type is returned by server");
    }

    if (cmd_type == CMD_ERROR) {
        if (packet.header.status == STATUS_DIR_NOTEMPTY) {
            printf("Directory %s is not empty\n", text_resp);
        } else if (packet.header.status == STATUS_DIR_NOTEXIST) {
            printf("Directory %s not exist\n", text_resp);
        } else if (packet.header.status == STATUS_NOT_DIR) {
            printf("%s is not a directory\n", text_resp);
        } else {
            printf("other error\n");
        }
    }

    // rmdir success
    if (cmd_type == CMD_ACK) {
        LOG_INFO("rmdir '%s' success", text_resp);
    }

    free_packet(&packet);
}

static void handle_rm(client_state_t* client_state, const command_request_t* req)
{
    send_packet(client_state->sock_fd, req->type, STATUS_OK, req->arg, strlen(req->arg));

    packet_t packet;
    char text_resp[MAX_PACKET_PAYLOAD];
    memset(&packet, 0, sizeof(packet));

    recv_packet(client_state->sock_fd, &packet);
    cmd_type_t cmd_type = packet.header.cmd_type;
    get_text_from_packet(&packet, text_resp, sizeof(text_resp));

    if (cmd_type != CMD_ACK && cmd_type != CMD_ERROR) {
        free_packet(&packet);
        LOG_ERROR("Unknown type is returned by server");
    }

    if (cmd_type == CMD_ERROR) {
        switch (packet.header.status) {
            case STATUS_IS_DIR:
                printf("Cannot delete directory '%s'\n", text_resp);
                break;
            case STATUS_FILE_NOTEXIST:
                printf("File '%s' is not found\n", text_resp);
                break;
            default:
                printf("other error\n");
                break;
        }
    }

    // rm success
    if (cmd_type == CMD_ACK) {
        LOG_INFO("rm file '%s' success", text_resp);
    }

    free_packet(&packet);
}

/*
 * PUTS upload flow:
 * 1. Expand and open the local file.
 * 2. Read file metadata: size, base name and SHA-256.
 * 3. Send CMD_PUTS_REQ with file_info_payload_t.
 * 4. Receive CMD_PUTS_RESP and parse the server resume offset.
 * 5. If offset == file_size, the server already has the full file.
 * 6. Otherwise seek to offset and send the remaining file data.
 * 7. Use mmap for large remaining data, read loop for smaller data.
 * 8. Send CMD_FILE_END and wait for the final ACK.
 */
static void handle_puts(client_state_t* client_state, const command_request_t* req)
{
    char local_path[PATH_MAX];      // Expanded absolute local path.
    char base_name[NAME_MAX];       // File name exposed to the remote directory.
    struct stat st;
    file_info_payload_t info;
    resume_payload_t resume;
    packet_t packet;
    uint64_t file_size;
    uint64_t file_offset;
    int file_fd = -1;

    // Resolve the local path first so later errors can report a stable path.
    if (utils_expand_local_path(req->arg, local_path, sizeof(local_path)) != 0) {
        fprintf(stderr, "invalid local path: %s\n", req->arg);
        return; 
    }

    // Open the local file and read its size.
    file_fd = open(local_path, O_RDONLY);
    if (file_fd == -1) {
        perror("open");
        return; 
    }

    memset(&st, 0, sizeof(st));
    if (fstat(file_fd, &st) != 0) {
        perror("fstat");
        close(file_fd);
        return; 
    }
    file_size = (uint64_t)st.st_size;

    // Upload only the base name; the client's local directory layout is private.
    if(utils_get_base_name(local_path, base_name, sizeof(base_name)) != 0) {
        fprintf(stderr, "failed to get base name from %s\n", local_path);
        close(file_fd);
        return;
    }

    // Hash the full file before upload so the server can deduplicate and verify it.
    memset(&info, 0, sizeof(info));
    if (utils_get_file_sha256(file_fd, info.sha256_hex, sizeof(info.sha256_hex)) != 0) {
        fprintf(stderr, "failed to get sha256 value from %s\n", base_name);
        close(file_fd);
        return;
    }
    if (lseek(file_fd, 0, SEEK_SET) == -1) {
        perror("lseek");
        close(file_fd);
        return;
    }

    // Build the request payload after all metadata is ready.
    snprintf(info.file_name, sizeof(info.file_name), "%s", base_name);
    info.file_size = host_to_net_u64(file_size);

    // Ask the server where this upload should start or whether it can be skipped.
    if (send_packet(client_state->sock_fd, req->type, STATUS_OK, &info, sizeof(info)) != 0) {
        close(file_fd);
        return;
    }

    // The server owns resume state, so trust only the offset returned by it.
    memset(&packet, 0, sizeof(packet));
    if(recv_packet(client_state->sock_fd, &packet) != 0) {
        free_packet(&packet);
        close(file_fd);
        return;
    }

    if (packet.header.cmd_type != CMD_PUTS_RESP) {
        fprintf(stderr,
                "puts failed: expected CMD_PUTS_RESP, got %s, status=%u\n",
                cmd_type_to_str((cmd_type_t)packet.header.cmd_type),
                packet.header.status);

        if (packet.header.data_len > 0) {
            print_text_from_packet(&packet);
        }
        free_packet(&packet);
        close(file_fd);
        return;
    }

    memcpy(&resume, packet.payload, sizeof(resume));
    file_offset = net_to_host_u64(resume.offset);
    free_packet(&packet);

    if (file_offset > file_size) {
        LOG_ERROR("Invalid resume position: %llu", file_offset);
        close(file_fd);
        return;
    }

    // Instant upload path: no file data is needed, only the end marker.
    if (file_offset ==  file_size) {
        printf("Use instant upload\n");
        close(file_fd);
        printf("puts completed\n");
        return;
    }

    if (file_offset > 0) {
        printf("resume puts from position %lu\n", file_offset);
    }

    // Resume from the byte position selected by the server.
    if (lseek(file_fd, file_offset, SEEK_SET) == -1) {
        close(file_fd);
        send_packet(client_state->sock_fd, CMD_ERROR, STATUS_IO_ERROR, NULL, 0); 
        perror("lseek");
        return;
    }

    // Large remaining uploads use mmap to avoid an extra userspace read buffer.
    if (file_size - file_offset > FILE_OPTIMIZATION_THRESHOLD) {
        // mmap offsets must be page-aligned, so map from the previous page boundary.
        long page_size = sysconf(_SC_PAGE_SIZE);
        if (page_size == -1) {
            close(file_fd);
            perror("sysconf");
            return;
        }

        uint64_t map_offset = file_offset / page_size * page_size;
        uint64_t offset_delta = file_offset - map_offset;
        uint64_t map_len = file_size - map_offset;

        char* map = mmap(NULL, map_len, PROT_READ, MAP_SHARED, file_fd, map_offset);
        if (map == MAP_FAILED) {
            close(file_fd);
            perror("mmap");
            return;
        }

        char* p = map + offset_delta;   // First unsent byte inside the mapped range.
        uint64_t remaining = file_size - file_offset; 

        // Send the mapped file range as protocol-sized chunks.
        while (remaining > 0) {
            uint32_t nsend = remaining > FILE_CHUNK_SIZE ? FILE_CHUNK_SIZE : (uint32_t)remaining;
            if (send_packet(client_state->sock_fd, CMD_FILE_DATA, STATUS_OK, p, nsend) != 0) {
                munmap(map, map_len);
                close(file_fd);
                fprintf(stderr, "failed to send file chunk\n");
                return;
            }
            p += nsend;
            remaining -= nsend;
        }
        munmap(map, map_len);
    } else {
        // Smaller remaining uploads use a simple read/send loop.
        uint64_t remaining = file_size - file_offset; 
        char buffer[FILE_CHUNK_SIZE];

        while (remaining > 0) {
            ssize_t nread = read(file_fd, buffer, sizeof(buffer)); 
            if (nread < 0) {
                if (errno == EINTR) {
                    continue;
                }
                close(file_fd);
                perror("read");
                return;
            }    
            // EOF before the expected byte count means the local file changed mid-upload.
            if (nread == 0) {
                break;
            }
            if (send_packet(client_state->sock_fd, CMD_FILE_DATA, STATUS_OK, buffer, nread) != 0) {
                close(file_fd);
                fprintf(stderr, "failed to send file chunk\n");
                return;
            }
            remaining -= nread;
        }
    }

    close(file_fd);

    // Mark the logical end of this upload attempt, including instant upload.
    if (send_packet(client_state->sock_fd, CMD_FILE_END, STATUS_OK, NULL, 0U) != 0) {
        fprintf(stderr, "failed to send the FILE_END flag\n");
        return;
    }

    memset(&packet, 0, sizeof(packet));
    if (recv_packet(client_state->sock_fd, &packet) != 0 || packet.header.cmd_type != CMD_ACK) {
        fprintf(stderr,
                "puts failed: expected CMD_ACK, got %s, status=%u\n",
                cmd_type_to_str((cmd_type_t)packet.header.cmd_type),
                packet.header.status);

        if (packet.header.data_len > 0) {
            print_text_from_packet(&packet);
        }
        free_packet(&packet);
        return;
    }

    printf("puts completed\n");
    free_packet(&packet);
}

/**
 * Client GETS download flow:
 *
 * 1. Send CMD_GETS_REQ with the requested remote path.
 * 2. Receive CMD_GETS_RESP containing file_name, file_size and sha256_hex.
 * 3. Build local paths: download_dir/file_name and download_dir/file_name.part.
 * 4. If the final local file already matches the remote metadata, report done.
 * 5. Otherwise use the .part file size as the resume offset.
 * 6. Send CMD_RESUME_POS(offset) to the server.
 * 7. Receive FILE_DATA packets until the missing byte range is complete.
 * 8. Receive CMD_FILE_END, verify SHA-256, ACK, then rename .part to final file.
 */
static void handle_gets(client_state_t* client_state, const command_request_t* req)
{
    packet_t packet;
    file_info_payload_t info;
    resume_payload_t resume;
    int file_fd;
    struct stat st;

    char file_name[NAME_MAX];
    char remote_sha256_hex[65];
    char local_sha256_hex[65];
    char part_sha256_hex[65];

    uint64_t remote_file_size;

    uint64_t offset;
    uint64_t recved = 0;

    char local_file_path[PATH_MAX + NAME_MAX + 1];     // download_dir/file_name
    char local_temp_file_path[PATH_MAX + NAME_MAX + 10];// download_dir/file_name.part
    unsigned int chunk_size;
    int nwrited;

    // 1. Ask the server for the requested remote file.
    if (send_packet(client_state->sock_fd, req->type, STATUS_OK, req->arg, strlen(req->arg)) != 0) {
        fprintf(stderr, "failed to send gets request\n");
        return;
    }

    // 2. Receive remote file metadata before choosing a local resume offset.
    memset(&packet, 0, sizeof(packet));
    if (recv_packet(client_state->sock_fd, &packet) != 0) {
        free_packet(&packet);
        return;
    }

    memset(&info, 0, sizeof(info));
    memcpy(&info, packet.payload, packet.header.data_len);
    strncpy(file_name, info.file_name, sizeof(file_name) - 1);
    file_name[sizeof(file_name) - 1] = '\0';
    remote_file_size = net_to_host_u64(info.file_size);
    strncpy(remote_sha256_hex, info.sha256_hex, sizeof(remote_sha256_hex) - 1);
    remote_sha256_hex[sizeof(remote_sha256_hex) - 1] = '\0';


    // 3. Build final and temporary local paths under the configured download dir.
    snprintf(local_file_path, sizeof(local_file_path), 
             "%s/%s", client_state->download_dir, file_name);
    snprintf(local_temp_file_path, sizeof(local_temp_file_path), 
             "%s/%s.part", client_state->download_dir, file_name);

    if (access(local_file_path, F_OK) == 0) {
        // 4. A complete local file can skip downloading only if size and hash match.
        if ((file_fd = open(local_file_path, O_RDONLY)) == -1) {
            perror("open file");
            send_packet(client_state->sock_fd, CMD_RESUME_POS, STATUS_IO_ERROR, NULL, 0); 
            return;
        }

        if (fstat(file_fd, &st) != 0) {
            perror("fstat");
            close(file_fd);
            send_packet(client_state->sock_fd, CMD_RESUME_POS, STATUS_IO_ERROR, NULL, 0); 
            return;
        }

        utils_get_file_sha256(file_fd, local_sha256_hex, sizeof(local_sha256_hex));

        if (strcasecmp(local_sha256_hex, remote_sha256_hex) == 0
            && (uint64_t)st.st_size == remote_file_size) {
            resume.offset = host_to_net_u64(st.st_size);
            send_packet(client_state->sock_fd, CMD_RESUME_POS, STATUS_OK, &resume, sizeof(resume));
            printf("file '%s' has already existed at local\n", file_name);
            return;
        }
        close(file_fd);
    }

    // 5. Resume from the existing .part file size, or start from zero.
    if ((file_fd = open(local_temp_file_path, O_CREAT | O_RDWR | O_APPEND, 0666)) == -1) {
        perror("open temp");
        send_packet(client_state->sock_fd, CMD_RESUME_POS, STATUS_IO_ERROR, NULL, 0); 
        return;
    }

    if (fstat(file_fd, &st) != 0) {
        perror("fstat");
        close(file_fd);
        send_packet(client_state->sock_fd, CMD_RESUME_POS, STATUS_IO_ERROR, NULL, 0); 
        return;
    }
    offset = st.st_size;
    if (offset > remote_file_size) {
        offset = 0;
        ftruncate(file_fd, offset);
    }
    resume.offset = host_to_net_u64(offset);

    // 6. Tell the server which byte range is still missing locally.
    send_packet(client_state->sock_fd, CMD_RESUME_POS, STATUS_OK, &resume, sizeof(resume));

    if (offset > 0) {
        printf("resume gets from position %lu\n", offset);
    }

    // 7. Receive exactly remote_file_size - offset bytes, then CMD_FILE_END.
    int got_file_end = 0;   // file end flag
    while (recved < remote_file_size - offset) {
        memset(&packet, 0, sizeof(packet));

        if (recv_packet(client_state->sock_fd, &packet) != 0) {
            send_packet(client_state->sock_fd, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
            close(file_fd);
            free_packet(&packet);
            return;
        }

        // The server may send FILE_END after the final data packet.
        if (packet.header.cmd_type == CMD_FILE_END) {
            got_file_end = 1;
            free_packet(&packet);
            break;
        }

        if (packet.header.cmd_type != CMD_FILE_DATA) {
            free_packet(&packet);
            send_packet(client_state->sock_fd, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
            close(file_fd);
            return;
        }

        chunk_size = packet.header.data_len; 
        if (chunk_size == 0 || chunk_size > FILE_CHUNK_SIZE) {
            free_packet(&packet);
            send_packet(client_state->sock_fd, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
            close(file_fd);
            return;
        }

        nwrited = write(file_fd, packet.payload, chunk_size);
        if (nwrited != (ssize_t)chunk_size) {
            free_packet(&packet);
            send_packet(client_state->sock_fd, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
            close(file_fd);
            return;
        }

        recved += chunk_size;
        free_packet(&packet);
    }

    if (recved == remote_file_size - offset) {
        if (!got_file_end) {
            if (recv_packet(client_state->sock_fd, &packet) != 0) {
                send_packet(client_state->sock_fd, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
                close(file_fd);
                return;
            }

            if (packet.header.cmd_type != CMD_FILE_END) {
                free_packet(&packet);
                send_packet(client_state->sock_fd, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
                close(file_fd);
                return;
            }
            free_packet(&packet);
        }

    }

    // 8. Verify the complete .part file before exposing it as the final file.
    if (lseek(file_fd, 0, SEEK_SET) != 0) {
        perror("lseek");
        close(file_fd);
        return;

    }
    utils_get_file_sha256(file_fd, part_sha256_hex, sizeof(part_sha256_hex));
    if (strcasecmp(part_sha256_hex, remote_sha256_hex) == 0) {
        // The server is ACKed only after the local content is verified.
        rename(local_temp_file_path, local_file_path);
        send_packet(client_state->sock_fd, CMD_ACK, STATUS_OK, NULL, 0);
        printf("gets file '%s' completed\n", file_name);
    } else {
        send_packet(client_state->sock_fd, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
    }
        close(file_fd);
}
