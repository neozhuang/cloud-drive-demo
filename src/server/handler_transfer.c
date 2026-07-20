#include "server/handler_transfer.h"

#include <fcntl.h>
#include <linux/limits.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/sendfile.h>

#include "common/log.h"
#include "common/protocol.h"
#include "common/utils.h"
#include "server/dao_basic.h"
#include "server/dao_status.h"
#include "server/session.h"
#include "server/dao_transfer.h"

static void handle_puts(transfer_task_t* task);
static void handle_gets(transfer_task_t* task);

static int send_transfer_packet(transfer_task_t *task,
                                cmd_type_t type,
                                status_code_t status,
                                const void *payload,
                                uint32_t payload_len)
{
    packet_t packet;

    if (packet_init(&packet, type, status,
                    &task->packet.header.session_id,
                    payload, payload_len) != 0) {
        return -1;
    }
    return protocol_send_packet(task->client_fd, &packet);
}

static int recv_transfer_packet(transfer_task_t *task, packet_t *packet)
{
    if (protocol_recv_packet(task->client_fd, packet) != 0) {
        return -1;
    }

    if (!session_id_equal(&packet->header.session_id,
                          &task->packet.header.session_id)) {
        packet_release(packet);
        (void)send_transfer_packet(task, CMD_ERROR, STATUS_UNAUTHORIZED, NULL, 0);
        return -1;
    }

    return 0;
}

void handle_transfer_task(void* arg)
{
    transfer_task_t *task = arg;

    LOG_INFO("fd=%d user=%s req=%s payload_len=%u",
             task->client_fd, task->session.username,
             cmd_type_to_str(task->packet.header.type),
             task->packet.header.payload_len);

    // Route by command type. Unknown commands are rejected as protocol errors.
    switch (task->packet.header.type) {
        // for puts request, the command is just like this:
        // puts ~/workspace/resources/book.md
        case CMD_PUTS_REQ:
            handle_puts(task); break;
        case CMD_GETS_REQ:
            handle_gets(task); break;
        default:
            send_transfer_packet(task, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
            break;
    }

    session_detach_fd(task->session_table, task->client_fd);
    shutdown(task->client_fd, SHUT_RDWR);
    close(task->client_fd);
    packet_release(&task->packet);
    free(task);
}


static void handle_puts(transfer_task_t* task)
{
    /*
     * PUTS upload flow:
     * 1. Parse the absolute remote path, size and SHA-256 from CMD_PUTS_REQ.
     * 2. Normalize the remote path and extract its file name.
     * 3. Resolve the destination parent directory.
     * 4. Reject the upload if the target virtual path already exists.
     * 6. Try instant upload by linking an existing physical file record.
     * 7. Otherwise resume or receive data into a temporary .part file.
     * 8. Verify the completed temporary file hash.
     * 9. Promote the temporary file into storage_root and create DB records.
     * 10. Send ACK only after both filesystem and database updates succeed.
     */

    upload_request_payload_t request;
    char file_name[NAME_MAX];
    uint64_t file_size;
    char sha256_hex[65];

    char target_path[PATH_MAX];  // Uploaded file path in the user's virtual tree.
    char parent_path[PATH_MAX];
    char part_path[PATH_MAX];
    char final_path[PATH_MAX];
    uint64_t user_id = task->session.user_id;
    uint64_t parent_id;
    uint64_t path_id;       // Existing virtual node id, only used for existence checks.
    uint64_t file_id;       // Physical file id in files table.

    int file_fd;
    struct stat st;
    uint64_t offset;
    uint64_t expected;
    uint64_t received = 0;
    packet_t packet;
    int got_file_end = 0;

    // 1. Parse upload metadata from the request payload.
    if (task->packet.header.payload_len != sizeof(request) ||
        task->packet.payload == NULL) {
        send_transfer_packet(task, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
        return;
    }

    memcpy(&request, task->packet.payload, sizeof(request));

    if (memchr(request.remote_path, '\0', sizeof(request.remote_path)) == NULL ||
        memchr(request.sha256_hex, '\0', sizeof(request.sha256_hex)) == NULL ||
        request.remote_path[0] != '/' ||
        !utils_is_valid_sha256_hex(request.sha256_hex)) {
        send_transfer_packet(task, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    file_size = net_to_host_u64(request.file_size);
    memcpy(sha256_hex, request.sha256_hex, sizeof(sha256_hex));

    // Upload destinations are independent of the session's current directory.
    if (normalize_cd_path("/", request.remote_path,
                          target_path, sizeof(target_path)) != 0 ||
        strcmp(target_path, "/") == 0) {
        send_transfer_packet(task, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    char *base = strrchr(target_path, '/');
    size_t file_name_len = strlen(base + 1);
    if (file_name_len == 0 || file_name_len >= sizeof(file_name)) {
        send_transfer_packet(task, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }
    memcpy(file_name, base + 1, file_name_len + 1);

    if (base == target_path) {
        memcpy(parent_path, "/", 2);
    } else {
        size_t parent_len = (size_t)(base - target_path);
        memcpy(parent_path, target_path, parent_len);
        parent_path[parent_len] = '\0';
    }

    int parent_ret = dao_path_get_node_id(task->db_pool,
                                           user_id,
                                           parent_path,
                                           &parent_id);
    if (parent_ret == DAO_PATH_NOT_FOUND || parent_ret == DAO_PATH_IS_FILE) {
        send_transfer_packet(task, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }
    if (parent_ret != DAO_PATH_IS_DIR) {
        send_transfer_packet(task, CMD_ERROR, STATUS_OTHER_ERROR, NULL, 0);
        return;
    }

    // 5. The upload creates a new virtual file, so the destination must not exist.
    int path_ret = dao_path_get_node_id(task->db_pool,
                                        user_id,
                                        target_path,
                                        &path_id);
    if (path_ret == DAO_PATH_IS_DIR || path_ret == DAO_PATH_IS_FILE) {
        send_transfer_packet(task, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    if (path_ret != DAO_PATH_NOT_FOUND) {
        send_transfer_packet(task, CMD_ERROR, STATUS_OTHER_ERROR, NULL, 0);
        return;
    }
    // 6. Check whether the physical file already exists for instant upload.
    int file_ret = dao_file_find_by_hash(task->db_pool,
                                         sha256_hex,
                                         file_size,
                                         &file_id);
    if (file_ret < 0) {
        send_transfer_packet(task, CMD_ERROR, STATUS_OTHER_ERROR, NULL, 0);
        return;
    }

    // Instant upload: create only the user's virtual path; no file bytes are needed.
    if (file_ret == 1) {
        if (dao_link_existing_file(task->db_pool,
                                   user_id,
                                   target_path,
                                   parent_id,
                                   file_name,
                                   file_id) != 0) {
            send_transfer_packet(task, CMD_ERROR, STATUS_OTHER_ERROR, NULL, 0);
            return;
        }

        // Tell the client the whole file is already available on the server.
        if (send_puts_resume(task->client_fd,
                             &task->packet.header.session_id,
                             file_size) != 0) {
            return;
        }
        send_transfer_packet(task, CMD_ACK, STATUS_OK, NULL, 0);
        return;
    }

    // 7. No existing file: use a hash-named temporary file for resumable upload.
    if (snprintf(part_path, sizeof(part_path), "%s/%s.part",
                 task->transfer_temp_dir, sha256_hex) >= (int)sizeof(part_path) ||
        snprintf(final_path, sizeof(final_path), "%s/%s",
                 task->storage_root, sha256_hex) >= (int)sizeof(final_path)) {
        send_transfer_packet(task, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // Keep partial data across disconnects so a later PUTS can resume from st_size.
    file_fd = open(part_path, O_CREAT | O_RDWR, 0666);
    if (file_fd < 0) {
        send_transfer_packet(task, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
        return;
    }

    if (fstat(file_fd, &st) != 0) {
        close(file_fd);
        send_transfer_packet(task, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
        return;
    }

    // If a previous partial upload exists, resume from its current size.
    offset = (uint64_t)st.st_size;
    if (offset > file_size) {
        if (ftruncate(file_fd, 0) != 0) {
            close(file_fd);
            send_transfer_packet(task, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
            return;
        }
        offset = 0;
    }

    // Tell the client where to start sending file data.
    if (send_puts_resume(task->client_fd,
                         &task->packet.header.session_id,
                         offset) != 0) {
        close(file_fd);
        return;
    }

    if (offset < file_size) {
        if (lseek(file_fd, (off_t)offset, SEEK_SET) == (off_t)-1) {
            close(file_fd);
            send_transfer_packet(task, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
            return;
        }

        expected = file_size - offset;

        // Receive exactly the missing byte range, followed by CMD_FILE_END.
        while (received < expected) {
            memset(&packet, 0, sizeof(packet));

            if (recv_transfer_packet(task, &packet) != 0) {
                close(file_fd);
                return;
            }

            if (packet.header.type == CMD_FILE_END) {
                got_file_end = 1;
                packet_release(&packet);
                break;
            }

            if (packet.header.type != CMD_FILE_DATA ||
                packet.header.payload_len == 0 ||
                packet.header.payload_len > FILE_CHUNK_SIZE ||
                packet.header.payload_len > expected - received) {
                packet_release(&packet);
                close(file_fd);
                send_transfer_packet(task, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
                return;
            }

            if (write_n(file_fd, packet.payload, packet.header.payload_len) != 0) {
                LOG_ERROR("failed to write upload temp file %s: %s", part_path, strerror(errno));
                packet_release(&packet);
                close(file_fd);
                send_transfer_packet(task, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
                return;
            }

            received += packet.header.payload_len;
            packet_release(&packet);
        }

        if (received != expected) {
            close(file_fd);
            send_transfer_packet(task, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
            return;
        }

        if (!got_file_end) {
            memset(&packet, 0, sizeof(packet));

            if (recv_transfer_packet(task, &packet) != 0) {
                close(file_fd);
                return;
            }

            if (packet.header.type != CMD_FILE_END) {
                packet_release(&packet);
                close(file_fd);
                send_transfer_packet(task, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
                return;
            }

            packet_release(&packet);
        }
    }

    // 8. Flush and verify the completed temporary file before publishing it.
    if (fsync(file_fd) != 0) {
        close(file_fd);
        send_transfer_packet(task, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
        return;
    }

    if (lseek(file_fd, 0, SEEK_SET) == (off_t)-1) {
        close(file_fd);
        send_transfer_packet(task, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
        return;
    }

    char actual_sha256[65];
    if (utils_get_file_sha256(file_fd, actual_sha256, sizeof(actual_sha256)) != 0) {
        close(file_fd);
        send_transfer_packet(task, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
        return;
    }

    close(file_fd);
    file_fd = -1;

    // A hash mismatch means the temporary file is corrupt or not the requested file.
    if (strcmp(actual_sha256, sha256_hex) != 0) {
        unlink(part_path);
        send_transfer_packet(task, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // 9. Publish the verified content under storage_root/<sha256>.
    if (rename(part_path, final_path) != 0) {
        if (errno == EEXIST || access(final_path, F_OK) == 0) {
            unlink(part_path);
        } else {
            send_transfer_packet(task, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
            return;
        }
    }

    // Create the physical file row and the user's virtual path in one DB operation.
    if (dao_create_file_with_path(task->db_pool,
                                  user_id,
                                  target_path,
                                  parent_id,
                                  file_name,
                                  sha256_hex,
                                  file_size) != 0) {
        send_transfer_packet(task, CMD_ERROR, STATUS_OTHER_ERROR, NULL, 0);
        return;
    }

    // 10. The upload is visible only after all previous steps have succeeded.
    send_transfer_packet(task, CMD_ACK, STATUS_OK, NULL, 0);
}


static int sendfile_exact(int out_fd, int in_fd, off_t *offset, uint64_t count)
{

    uint64_t remaining = count;

    while (remaining > 0) {
        ssize_t n = sendfile(out_fd, in_fd, offset, (size_t)remaining);

        if (n > 0) {
            remaining -= (uint64_t)n;
            continue;
        }

        if (n == 0) {
            return -1;
        }

        if (errno == EINTR) {
            continue;
        }

        return -1;
    }
    return 0;
}


/*
 * GETS download flow:
 * 1. Parse CMD_GETS_REQ payload as the requested remote path.
 * 2. Resolve that path against the user's current virtual cwd.
 * 3. Load file metadata from paths/files and build storage_root/<sha256>.
 * 4. Send CMD_GETS_RESP with file_name, file_size and sha256_hex.
 * 5. Receive CMD_RESUME_POS; the client owns download resume state.
 * 6. Send the remaining bytes as FILE_DATA headers plus sendfile payloads.
 * 7. Send CMD_FILE_END and read the client's final ACK.
 */
static void handle_gets(transfer_task_t* task)
{

    char req_path[PATH_MAX];     // Remote path argument from the client command.
    char target_path[PATH_MAX];
    char physical_path[PATH_MAX + NAME_MAX + 1];

    uint64_t user_id = task->session.user_id;

    file_meta_t file_meta;
    file_info_payload_t info;
    packet_t packet;
    resume_payload_t resume;

    uint64_t start_offset;
    int file_fd;
    struct stat st;

    int ret;

    // 1. Copy the requested remote path from the first GETS packet.
    if (task->packet.payload == NULL ||
        task->packet.header.payload_len == 0 ||
        task->packet.header.payload_len >= sizeof(req_path)) {
        send_transfer_packet(task, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }
    memcpy(req_path, task->packet.payload, task->packet.header.payload_len);
    req_path[task->packet.header.payload_len] = '\0';


    // Resolve relative paths like "a.txt" or "../a.txt" inside the virtual tree.
    if (normalize_cd_path(task->session.cwd, req_path,
                          target_path, sizeof(target_path)) != 0) {
        send_transfer_packet(task, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // 3. Look up file metadata through paths -> files.
    ret = dao_get_file_meta(task->db_pool, user_id, target_path, &file_meta);

    switch (ret) {
        case DAO_OK:
            break;
        case DAO_NOT_FOUND:
            send_transfer_packet(task, CMD_ERROR, STATUS_FILE_NOTEXIST, NULL, 0);
            return;
        case DAO_TYPE_MISMATCH:
            send_transfer_packet(task, CMD_ERROR, STATUS_IS_DIR, NULL, 0);
            return;
        case DAO_DB_ERROR:
            send_transfer_packet(task, CMD_ERROR, STATUS_DB_ERROR, NULL, 0);
            return;
    }

    // The physical object is content-addressed by SHA-256 under storage_root.
    memset(physical_path, 0, sizeof(physical_path));
    snprintf(physical_path, sizeof(physical_path), 
             "%s/%s", task->storage_root, file_meta.sha256_hex);

    // 4. Send remote file metadata so the client can choose its resume offset.
    memset(&info, 0, sizeof(info));
    strncpy(info.file_name, file_meta.file_name, sizeof(info.file_name) - 1);
    info.file_name[sizeof(info.file_name) - 1] = '\0';
    info.file_size = host_to_net_u64(file_meta.size);
    strncpy(info.sha256_hex, file_meta.sha256_hex, sizeof(info.sha256_hex) - 1);
    info.sha256_hex[sizeof(info.sha256_hex) - 1] = '\0';

    if (send_transfer_packet(task, CMD_GETS_RESP, STATUS_OK,
                             &info, sizeof(info)) != 0) {
        return;
    }

    // 5. For downloads, the client owns resume state because .part is local.
    if (recv_transfer_packet(task, &packet) != 0) {
        return;
    }

    if (packet.header.type != CMD_RESUME_POS ||
        packet.payload == NULL ||
        packet.header.payload_len != sizeof(resume)) {
        packet_release(&packet);
        send_transfer_packet(task, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
        return;
    }

    memcpy(&resume, packet.payload, sizeof(resume));
    packet_release(&packet);

    if ((file_fd = open(physical_path, O_RDONLY)) == -1) {
        perror("open");
        return;
    }
    if (fstat(file_fd, &st) != 0) {
        perror("open");
        close(file_fd);
        return;
    }

    start_offset = net_to_host_u64(resume.offset);
    if (start_offset > (uint64_t)st.st_size) {
        close(file_fd);
        return;
    }
    off_t offset = start_offset;
    uint64_t remaining = st.st_size - start_offset;

    // 6. Send each TLV FILE_DATA header followed by exactly chunk_size bytes.
    while (remaining > 0) {
        uint32_t chunk_size =
            remaining > FILE_CHUNK_SIZE ? FILE_CHUNK_SIZE : (uint32_t)remaining;

        packet_header_t header = {
            .type = CMD_FILE_DATA,
            .status = STATUS_OK,
            .payload_len = chunk_size,
            .session_id = task->packet.header.session_id,
        };

        if (protocol_send_header(task->client_fd, &header) != 0) {
            close(file_fd);
            return;
        }

        if (sendfile_exact(task->client_fd, file_fd, &offset, chunk_size) != 0) {
            close(file_fd);
            return;
        }
        remaining -= chunk_size;
    }
    close(file_fd);

    // 7. Mark the logical end of the download stream.
    if (send_transfer_packet(task, CMD_FILE_END, STATUS_OK, NULL, 0U) != 0) {
        fprintf(stderr, "failed to send the FILE_END flag\n");
        return;
    }

    // The client ACKs only after writing and verifying the downloaded content.
    if (recv_transfer_packet(task, &packet) != 0) {
        fprintf(stderr, "failed to recv client ack\n");
        return;
    }
    if (packet.header.type != CMD_ACK) {
        fprintf(stderr, "recv wrong client ack\n");
    }
    packet_release(&packet);
}
