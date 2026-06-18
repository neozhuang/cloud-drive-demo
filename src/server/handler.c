#include "server/handler.h"

#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/epoll.h>

#include "common/log.h"
#include "common/protocol.h"
#include "common/utils.h"
#include "server/network.h"
#include "server/session.h"

/* Command handlers for one decoded client packet. */
static void handle_login(packet_task_t* task);
static void handle_pwd(packet_task_t* task);
static void handle_cd(packet_task_t* task);
static void handle_ls(packet_task_t* task);
static void handle_mkdir(packet_task_t* task);
static void handle_rmdir(packet_task_t* task);
static void handle_rm(packet_task_t* task);

static void handle_puts(transfer_task_t* task);
static void handle_gets(transfer_task_t* task);

static void format_response(char* response, size_t response_size,
                            const char* fallback, const char* fmt, const char* value)
{
    int n = snprintf(response, response_size, fmt, value);
    if (n < 0 || n >= (int)response_size) {
        snprintf(response, response_size, "%s", fallback);
    }
}


/*
 * Dispatch one packet task to the matching command handler.
 * The task owns the packet memory and is released after handling.
 */
void handle_packet_task(void *arg)
{
    packet_task_t *task = arg;
    char* username = session_get_username(task->client_fd);

    LOG_INFO("fd=%d user=%s req=%s payload_len=%u payload=%s",
             task->client_fd, username,
             cmd_type_to_str((cmd_type_t)task->packet.header.cmd_type),
             task->packet.header.data_len,
             task->packet.payload);

    // Route by command type. Unknown commands are rejected as protocol errors.
    switch (task->packet.header.cmd_type) {
        case CMD_LOGIN_REQ:
            handle_login(task);
            break;
        case CMD_PWD:
            handle_pwd(task);
            break;
        case CMD_CD:
            handle_cd(task);
            break;
        case CMD_LS:
            handle_ls(task);
            break;
        case CMD_MKDIR:
            handle_mkdir(task);
            break;
        case CMD_RMDIR:
            handle_rmdir(task);
            break;
        case CMD_RM:
            handle_rm(task);
            break;
        default:
            send_packet(task->client_fd, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
            break;
    }

    // Packet tasks are allocated by the network/thread-pool side for this call.
    free_packet(&task->packet);
    free(task);
}

/*
 */
void handle_transfer_task(void* arg)
{
    transfer_task_t *task = arg;
    char* username = session_get_username(task->client_fd);

    LOG_INFO("fd=%d user=%s req=%s payload_len=%u payload=%s",
             task->client_fd, username,
             cmd_type_to_str((cmd_type_t)task->packet.header.cmd_type),
             task->packet.header.data_len,
             task->packet.payload);

    // Route by command type. Unknown commands are rejected as protocol errors.
    switch (task->packet.header.cmd_type) {
        // for puts request, the command is just like this:
        // puts ~/workspace/resources/book.md
        case CMD_PUTS_REQ:
            handle_puts(task);
            break;
        case CMD_GETS_REQ:
            handle_gets(task);
            break;
        default:
            send_packet(task->client_fd, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
            break;
    }

    if (network_add_epoll_fd(task->epoll_fd, task->client_fd, EPOLLIN | EPOLLRDHUP) != 0) {
        session_destroy(task->client_fd);
        close(task->client_fd);
    }
    free_packet(&task->packet);
    free(task);
}
/*
 * Handle login by validating the username and preparing that user's root dir.
 * A successful login stores user state in the session table for later commands.
 */
static void handle_login(packet_task_t *task)
{
    char username[MAX_USERNAME_LEN];
    char user_root[PATH_MAX];

    // Copy the packet payload into a null-terminated username string.
    memset(username, 0, sizeof(username));
    memcpy(username, task->packet.payload, task->packet.header.data_len);
    username[task->packet.header.data_len] = '\0';

    if (auth_check_user(task->auth_config, username)) {
        // Map the username to a real filesystem directory under cloud_drive_root.
        if (join_path(user_root, sizeof(user_root), task->cloud_drive_root, username) != 0) {
            send_packet(task->client_fd, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
            return;
        }
        // Ensure the user's root directory exists before accepting the login.
        if (mkdir_p(user_root, 0755) != 0) {
            LOG_ERROR("failed to create user root %s", user_root);
            send_packet(task->client_fd, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
            return;
        }
        // Store username, real root path, and initial virtual cwd in the session.
        if (session_set_login_state(task->client_fd, username, user_root) != 0) {
            send_packet(task->client_fd, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
            return;
        }
        LOG_INFO("user %s logined", username);
        send_packet(task->client_fd, CMD_ACK, STATUS_OK, NULL, 0);
    } else {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
    }
}

/* Reply with the current virtual working directory for this client session. */
static void handle_pwd(packet_task_t* task)
{
    char* cwd;

    // The session keeps a virtual path such as "/" or "/docs", not a real path.
    if ((cwd = session_get_cwd(task->client_fd)) == NULL) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
        return; 
    }
    send_packet(task->client_fd, CMD_ACK, STATUS_OK, cwd, strlen(cwd));
}

/* Remove the last component from a normalized virtual path. */
static int pop_path_component(char *path)
{
    char *slash;

    // The virtual root cannot move above itself.
    if (strcmp(path, "/") == 0) {
        return 0;
    }

    slash = strrchr(path, '/');
    if (slash == path) {
        // Popping "/name" returns to root while keeping a valid "/" string.
        path[1] = '\0';
    } else if (slash != NULL) {
        *slash = '\0';
    }

    return 0;
}

/* Append one path component to a normalized virtual path. */
static int append_path_component(char *path, size_t size, const char *component)
{
    int n;

    // Avoid producing paths with a double slash after the virtual root.
    if (strcmp(path, "/") == 0) {
        n = snprintf(path, size, "/%s", component);
    } else {
        n = snprintf(path + strlen(path), size - strlen(path), "/%s", component);
    }

    return n < 0 || n >= (int)(size - strlen(path)) ? -1 : 0;
}

/*
 * Normalize a client path into the server's virtual path space.
 * Handles absolute paths, relative paths, '.', and '..' without touching disk.
 */
static int normalize_cd_path(char *dst, size_t dst_size,
                             const char *cwd, const char *arg)
{
    char tmp[PATH_MAX];
    char *saveptr = NULL;
    char *part;

    // Absolute arguments start at virtual root; relative ones start at cwd.
    if (arg[0] == '/') {
        strncpy(dst, "/", dst_size - 1);
    } else {
        strncpy(dst, cwd, dst_size - 1);
    }
    dst[dst_size - 1] = '\0';

    // strtok_r edits its input, so work on a local copy of the argument.
    strncpy(tmp, arg, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    // Process one slash-separated component at a time.
    part = strtok_r(tmp, "/", &saveptr);
    while (part != NULL) {
        if (strcmp(part, ".") == 0 || strcmp(part, "") == 0) {
            /* skip */
        } else if (strcmp(part, "..") == 0) {
            pop_path_component(dst);
        } else {
            if (append_path_component(dst, dst_size, part) != 0) {
                return -1;
            }
        }

        part = strtok_r(NULL, "/", &saveptr);
    }

    return 0;
}

/*
 * Convert a client path argument to a real filesystem path under user_root.
 * The intermediate virtual path keeps clients confined to their own root.
 */
static int build_user_fs_path(char *dst, size_t dst_size,
                              const char *user_root, const char *cwd,
                              const char *arg)
{
    int n;
    char virtual_path[PATH_MAX];

    // First normalize in virtual space so '..' cannot escape user_root.
    if (normalize_cd_path(virtual_path, sizeof(virtual_path), cwd, arg) != 0) {
        return -1;
    }

    // Then map virtual root to the user's real root directory.
    if (strcmp(virtual_path, "/") == 0) {
        n = snprintf(dst, dst_size, "%s", user_root);
    } else {
        n = snprintf(dst, dst_size, "%s%s", user_root, virtual_path);
    }

    return n < 0 || n >= (int)dst_size ? -1 : 0;
}

/* Change the current virtual directory after checking the real target exists. */
static void handle_cd(packet_task_t* task)
{
    char arg[PATH_MAX];         // Raw cd argument from the client.
    char user_root[PATH_MAX];   // Absolute filesystem root for the logged-in user.
    char cwd[PATH_MAX];         // Current virtual cwd shown to the client.
    char new_cwd[PATH_MAX];     // Normalized virtual cwd after cd.
    char fs_path[PATH_MAX];     // Filesystem path mapped from new_cwd.
    char response[MAX_TEXT_PAYLOAD]; 
    struct stat st;

    memset(arg, 0, sizeof(arg));

    // Treat plain "cd" as "cd /", which returns to the user's virtual home.
    if (task->packet.header.data_len == 0) {
        strcpy(arg, "/");
    } else {
        memcpy(arg, task->packet.payload, task->packet.header.data_len);
        arg[task->packet.header.data_len] = '\0';
    }

    // Load both path spaces: the real user root and the current virtual cwd.
    if (session_get_paths(task->client_fd, user_root, sizeof(user_root), cwd, sizeof(cwd)) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // Resolve '.', '..', absolute paths, and relative paths into a virtual path.
    if (normalize_cd_path(new_cwd, sizeof(new_cwd), cwd, arg) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // Convert the virtual cwd into a real filesystem path under user_root.
    if (strcmp(new_cwd, "/") == 0) {
        if (snprintf(fs_path, sizeof(fs_path), "%s", user_root) >= (int)sizeof(fs_path)) {
            send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
            return;
        }
    } else if (snprintf(fs_path, sizeof(fs_path), "%s%s", user_root, new_cwd) >=
        (int)sizeof(fs_path)) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // cd can only switch to an existing directory.
    if (stat(fs_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        format_response(response, sizeof(response), "dir not exist", "dir %s not exist", arg);
        send_packet(task->client_fd, CMD_ERROR, STATUS_NOT_FOUND, response, strlen(response));
        return;
    }

    // Store only the virtual cwd in the session, never the real filesystem path.
    if (session_set_cwd(task->client_fd, new_cwd) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
        return;
    }

    send_packet(task->client_fd, CMD_ACK, STATUS_CD_OK, new_cwd, strlen(new_cwd));
}

/* List files in the requested virtual directory or the current directory. */
static void handle_ls(packet_task_t* task)
{
    char arg[PATH_MAX];       // Raw ls argument from the client.
    char user_root[PATH_MAX]; // Absolute filesystem root for the logged-in user.
    char cwd[PATH_MAX];       // Current virtual cwd shown to the client.
    char fs_path[PATH_MAX];   // Real filesystem path to list.
    char response[MAX_TEXT_PAYLOAD];
    DIR *dir;
    struct dirent *entry;
    size_t used = 0;

    memset(arg, 0, sizeof(arg));
    memset(response, 0, sizeof(response));


    // Plain "ls" has no argument and lists the current virtual cwd.
    if (task->packet.header.data_len > 0) {
        memcpy(arg, task->packet.payload, task->packet.header.data_len);
        arg[task->packet.header.data_len] = '\0';
    }

    // Load both path spaces: the real user root and the current virtual cwd.
    if (session_get_paths(task->client_fd, user_root, sizeof(user_root), cwd, sizeof(cwd)) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    if (arg[0] == '\0') {
        // No argument: list the current directory.
        if (strcmp(cwd, "/") == 0) {
            if (snprintf(fs_path, sizeof(fs_path), "%s", user_root) >= (int)sizeof(fs_path)) {
                send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
                return;
            }
        } else if (snprintf(fs_path, sizeof(fs_path), "%s%s", user_root, cwd) >=
            (int)sizeof(fs_path)) {
            send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
            return;
        }
    } else {
        // With an argument: list user_root + cwd + arg.
        if (build_user_fs_path(fs_path, sizeof(fs_path), user_root, cwd, arg) != 0) {
            send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
            return;
        }
    }

    // opendir verifies the target can currently be opened as a directory.
    dir = opendir(fs_path);
    if (dir == NULL) {
        format_response(response, sizeof(response), "dir not exist", "dir %s not exist", arg);
        send_packet(task->client_fd, CMD_ERROR, STATUS_NOT_FOUND, response, strlen(response));
        return;
    }

    // Read directory entries into a single text payload for the client.
    while ((entry = readdir(dir)) != NULL) {
        int n;
        struct stat st;
        int is_dir = 0;

        // Hide the implementation entries for current and parent directory.
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        // Check the entry relative to the opened directory; directories get a '/'.
        if (fstatat(dirfd(dir), entry->d_name, &st, 0) == 0) {
            is_dir = S_ISDIR(st.st_mode);
        }

        if (is_dir) {
            n = snprintf(response + used, sizeof(response) - used, "%s/\n", entry->d_name);
        } else {
            n = snprintf(response + used, sizeof(response) - used, "%s\n", entry->d_name);
        }

        // Stop if the packet payload buffer is full; send what has been collected.
        if (n < 0 || n >= (int)(sizeof(response) - used)) {
            break;
        }

        used += (size_t)n;
    }

    closedir(dir);
    send_packet(task->client_fd, CMD_ACK, STATUS_OK, response, strlen(response));

}

/* Create one directory under the user's virtual cwd or requested path. */
static void handle_mkdir(packet_task_t* task)
{
    char arg[PATH_MAX];       // Raw mkdir argument from the client.
    char user_root[PATH_MAX]; // Absolute filesystem root for the logged-in user.
    char cwd[PATH_MAX];       // Current virtual cwd shown to the client.
    char fs_path[PATH_MAX];   // Real filesystem path to create.
    char response[MAX_TEXT_PAYLOAD];
    memset(arg, 0, sizeof(arg));

    memcpy(arg, task->packet.payload, task->packet.header.data_len);
    arg[task->packet.header.data_len] = '\0';

    // Load both path spaces: the real user root and the current virtual cwd.
    if (session_get_paths(task->client_fd, user_root, sizeof(user_root), cwd, sizeof(cwd)) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // Build the real path with the simple rule: user_root + cwd + arg.
    if (build_user_fs_path(fs_path, sizeof(fs_path), user_root, cwd, arg) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // Let mkdir decide whether the path already exists or the parent is missing.
    if (mkdir(fs_path, 0755) != 0) {
        if (errno == EEXIST) {
            format_response(response, sizeof(response), "dir has existed", "dir %s has existed", arg);
        } else {
            snprintf(response, sizeof(response), "other error");
        }
        send_packet(task->client_fd, CMD_ERROR, STATUS_IO_ERROR, response, strlen(response));
        return;
    }

    send_packet(task->client_fd, CMD_ACK, STATUS_OK, NULL, 0);
}

/* Remove one empty directory, but never the user's root directory itself. */
static void handle_rmdir(packet_task_t* task)
{
    char arg[PATH_MAX];       // Raw rmdir argument from the client.
    char user_root[PATH_MAX]; // Absolute filesystem root for the logged-in user.
    char cwd[PATH_MAX];       // Current virtual cwd shown to the client.
    char fs_path[PATH_MAX];   // Real filesystem path to remove.
    char response[MAX_TEXT_PAYLOAD];    

    memset(arg, 0, sizeof(arg));

    memcpy(arg, task->packet.payload, task->packet.header.data_len);
    arg[task->packet.header.data_len] = '\0';

    // Load both path spaces: the real user root and the current virtual cwd.
    if (session_get_paths(task->client_fd, user_root, sizeof(user_root), cwd, sizeof(cwd)) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // Build the real path with the simple rule: user_root + cwd + arg.
    if (build_user_fs_path(fs_path, sizeof(fs_path), user_root, cwd, arg) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // Do not allow removing the session root, even if the argument resolves to it.
    if (strcmp(fs_path, user_root) == 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // rmdir removes only an existing empty directory.
    if (rmdir(fs_path) != 0) {
        if (errno == ENOTEMPTY) {
            format_response(response, sizeof(response), "dir not empty", "dir %s not empty", arg);
        } else if (errno == ENOENT) {
            format_response(response, sizeof(response), "dir not exist", "dir %s not exist", arg);
        } else if (errno == ENOTDIR) {
            format_response(response, sizeof(response), "not a directory", "%s is not a directory", arg);
        } else {
            snprintf(response, sizeof(response), "other error");
        }
        send_packet(task->client_fd, CMD_ERROR, STATUS_IO_ERROR, response, strlen(response));
        return;
    }

    send_packet(task->client_fd, CMD_ACK, STATUS_OK, NULL, 0);
}

/* Remove one non-directory filesystem entry under the user's virtual path. */
static void handle_rm(packet_task_t* task)
{
    char arg[PATH_MAX];       // Raw rm argument from the client.
    char user_root[PATH_MAX]; // Absolute filesystem root for the logged-in user.
    char cwd[PATH_MAX];       // Current virtual cwd shown to the client.
    char fs_path[PATH_MAX];   // Real filesystem path to remove.
    char response[MAX_TEXT_PAYLOAD];

    memset(arg, 0, sizeof(arg));

    memcpy(arg, task->packet.payload, task->packet.header.data_len);
    arg[task->packet.header.data_len] = '\0';

    // Load both path spaces: the real user root and the current virtual cwd.
    if (session_get_paths(task->client_fd, user_root, sizeof(user_root), cwd, sizeof(cwd)) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // Build the real path with the simple rule: user_root + cwd + arg.
    if (build_user_fs_path(fs_path, sizeof(fs_path), user_root, cwd, arg) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // unlink removes a file. Directories should be removed by rmdir.
    if (unlink(fs_path) != 0) {
        if (errno == ENOENT) {
            format_response(response, sizeof(response), "file not exist", "file %s not exist", arg);
        } else {
            snprintf(response, sizeof(response), "other error");
        }
        send_packet(task->client_fd, CMD_ERROR, STATUS_IO_ERROR, response, sizeof(response));
        return;
    }

    send_packet(task->client_fd, CMD_ACK, STATUS_OK, NULL, 0);
}


/**
 * handle_puts
 *
 * The process of server side puts:
 * 1. recv puts_req
 * 2. send puts_resp
 * 3. recv puts_file_data
 * 4. recv puts_file_end
 * 5. send ack/error
 */
static void handle_puts(transfer_task_t* task)
{
    char file_name[NAME_MAX]; // The uploaded file name
    char user_root[PATH_MAX]; // Absolute filesystem root for the logged-in user.
    char cwd[PATH_MAX];       // Current virtual cwd shown to the client.
    char fs_path[PATH_MAX];   // Real filesystem path to remove.
    char response[MAX_TEXT_PAYLOAD];
    uint64_t file_size;
    file_info_payload_t info;
    uint64_t recved = 0;
    packet_t packet;
    file_chunk_payload_t chunk;
    unsigned int chunk_size;
    int file_fd;
    int nwrited;

    // get the uploaded file name and file size
    memset(&info, 0, sizeof(info));
    memcpy(&info, task->packet.payload, task->packet.header.data_len);
    file_size = net_to_host_u64(info.file_size);
    memcpy(file_name, info.file_name, strlen(info.file_name));
    file_name[strlen(info.file_name)] = '\0';

    // Load both path spaces: the real user root and the current virtual cwd.
    if (session_get_paths(task->client_fd, user_root, sizeof(user_root), cwd, sizeof(cwd)) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // Build the real path with the simple rule: user_root + cwd + file_name.
    if (build_user_fs_path(fs_path, sizeof(fs_path), user_root, cwd, file_name) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // open the file
    if ((file_fd = open(fs_path, O_CREAT | O_RDWR | O_TRUNC, 0666))  == -1) {
        LOG_ERROR("failed to open the uploaded file %s", file_name);
        format_response(response, sizeof(response), "server failed to open file\n",
                        "server failed to open file %s\n", file_name);
        send_packet(task->client_fd, CMD_ERROR, STATUS_IO_ERROR, response, strlen(response));
        return;
    }

    // send puts_resp
    send_packet(task->client_fd, CMD_PUTS_RESP, STATUS_OK, NULL, 0);

    // recv file data
    int got_file_end = 0;   // file end flag
    while (recved < file_size) {
        memset(&packet, 0, sizeof(packet));

        if (recv_packet(task->client_fd, &packet) != 0) {
            send_packet(task->client_fd, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
            close(file_fd);
            return;
        }

        if (packet.header.cmd_type == CMD_FILE_END) {
            got_file_end = 1;
            free_packet(&packet);
            break;
        }

        if (packet.header.cmd_type != CMD_FILE_DATA) {
            free_packet(&packet);
            send_packet(task->client_fd, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
            close(file_fd);
            return;
        }

        memcpy(&chunk, packet.payload, sizeof(chunk));

        chunk_size = ntohl(chunk.data_len);
        if (chunk_size == 0 || chunk_size > FILE_BLOCK_SIZE) {
            free_packet(&packet);
            send_packet(task->client_fd, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
            close(file_fd);
            return;
        }

        nwrited = write(file_fd, chunk.data, chunk_size);
        if (nwrited != (ssize_t)chunk_size) {
            free_packet(&packet);
            send_packet(task->client_fd, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
            close(file_fd);
            return;
        }

        recved += chunk_size;
        free_packet(&packet);
    }

    // send ack
    if (recved == file_size) {
        if (!got_file_end) {
            if (recv_packet(task->client_fd, &packet) != 0) {
                send_packet(task->client_fd, CMD_ERROR, STATUS_IO_ERROR, NULL, 0);
                close(file_fd);
                return;
            }

            if (packet.header.cmd_type != CMD_FILE_END) {
                free_packet(&packet);
                send_packet(task->client_fd, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
                close(file_fd);
                return;
            }
            free_packet(&packet);
        }

        close(file_fd);
        send_packet(task->client_fd, CMD_ACK, STATUS_OK, NULL, 0);
    } else {
        close(file_fd);
        send_packet(task->client_fd, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
    }
}


/**
 * handle_gets
 *
 * the process of server gets:
 *
 * 1. recv GETS_REQ
 * 2. open file
 * 3. send GETS_RESP(file info)
 * 4. loop send CMD_FILE_DATA
 * 5. send CMD_FILE_END
 * 6. recv ACK or ERROR
 */
static void handle_gets(transfer_task_t* task)
{
    char file_name[PATH_MAX]; // The downloaded file name, e.x., gets /docs/readme.md
    char base_name[NAME_MAX]; // the downloaded base file name, e.x., readme.md
    char user_root[PATH_MAX]; // Absolute filesystem root for the logged-in user.
    char cwd[PATH_MAX];       // Current virtual cwd shown to the client.
    char fs_path[PATH_MAX];   // Real filesystem path to remove.
    char response[MAX_TEXT_PAYLOAD];
    uint64_t file_size;
    file_info_payload_t info;
    packet_t packet;
    file_chunk_payload_t chunk;
    int file_fd;
    struct stat st;

    // get the downloaded file name and file size
    memcpy(file_name, task->packet.payload, task->packet.header.data_len);
    file_name[task->packet.header.data_len] = '\0';

    // Load both path spaces: the real user root and the current virtual cwd.
    if (session_get_paths(task->client_fd, user_root, sizeof(user_root), cwd, sizeof(cwd)) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // Build the real path with the simple rule: user_root + cwd + file_name.
    if (build_user_fs_path(fs_path, sizeof(fs_path), user_root, cwd, file_name) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // open the file
    if ((file_fd = open(fs_path, O_RDONLY))  == -1) {
        LOG_ERROR("failed to open the downloaded file %s", file_name);
        format_response(response, sizeof(response), "server failed to open file\n",
                        "server failed to open file %s\n", file_name);
        send_packet(task->client_fd, CMD_ERROR, STATUS_IO_ERROR, response, strlen(response));
        return;
    }


    if (fstat(file_fd, &st) != 0) {
        perror("fstat");
        close(file_fd);
        return;
    }
    file_size = (uint64_t)st.st_size;

    // fill the payload
    memset(&info, 0, sizeof(info));
    if (get_base_name(fs_path, base_name, sizeof(base_name)) != 0) {
        close(file_fd);
        return;
    }
    strncpy(info.file_name, base_name, strlen(base_name));
    info.file_name[strlen(base_name)] = '\0';
    info.file_size = host_to_net_u64(file_size);

    // send GETS_REQ(file_name, file_size)
    if (send_packet(task->client_fd, CMD_GETS_RESP, STATUS_OK, &info, sizeof(info)) != 0) {
        close(file_fd);
        return;
    }

    // loop: send FILE_DATA
    uint64_t sended = 0;
    while (sended < file_size) {
        memset(&chunk, 0, sizeof(chunk));
        ssize_t nread = read(file_fd, chunk.data, sizeof(chunk.data));
        printf("file_fd = %d\n", file_fd);
        if (nread < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(file_fd);
            perror("read");
            return;
        }    
        // read eof
        if (nread == 0) {
            break;
        }
        chunk.data_len = htonl(nread);
        if (send_packet(task->client_fd, CMD_FILE_DATA, STATUS_OK, &chunk, sizeof(chunk)) != 0) {
            close(file_fd);
            fprintf(stderr, "failed to send file chunk\n");
            return;
        }
        sended += nread;
    }
    close(file_fd);

    // 5. send FILE_END
    if (send_packet(task->client_fd, CMD_FILE_END, STATUS_OK, NULL, 0U) != 0) {
        fprintf(stderr, "failed to send the FILE_END flag\n");
        return;
    }

    // 6. recv ack/error
    if (recv_packet(task->client_fd, &packet) != 0) {
        fprintf(stderr, "failed to recv client ack\n");
    }
    if (packet.header.cmd_type != CMD_ACK) {
        fprintf(stderr, "recv wrong client ack\n");
    }
    free_packet(&packet);
}
