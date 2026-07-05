#include "server/handler_basic.h"

#include <linux/limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <crypt.h>
#include <libgen.h>

#include "common/log.h"
#include "common/protocol.h"
#include "common/utils.h"
#include "server/dao_status.h"
#include "server/session.h"
#include "server/dao_auth.h"
#include "server/dao_basic.h"


/* Command handlers for one decoded client packet. */
static void handle_register(packet_task_t* task);
static void handle_login(packet_task_t* task);
static void handle_pwd(packet_task_t* task);
static void handle_cd(packet_task_t* task);
static void handle_ls(packet_task_t* task);
static void handle_mkdir(packet_task_t* task);
static void handle_rmdir(packet_task_t* task);
static void handle_rm(packet_task_t* task);


/*
 * Dispatch one packet task to the matching command handler.
 * The task owns the packet memory and is released after handling.
 */
void handle_basic_task(void *arg)
{
    packet_task_t *task = arg;
    char username[64];
    if (session_get_username(task->client_fd, username, sizeof(username)) != 0) {
        free_packet(&task->packet);
        free(task);
        return;   
    }

    LOG_INFO("fd=%d user=%s req=%s payload_len=%u",
             task->client_fd, username,
             cmd_type_to_str((cmd_type_t)task->packet.header.cmd_type),
             task->packet.header.data_len);

    // Route by command type. Unknown commands are rejected as protocol errors.
    switch (task->packet.header.cmd_type) {
        case CMD_REGISTER_REQ:
            handle_register(task); break;
        case CMD_LOGIN_REQ:
            handle_login(task); break;
        case CMD_PWD:
            handle_pwd(task); break;
        case CMD_CD:
            handle_cd(task); break;
        case CMD_LS:
            handle_ls(task); break;
        case CMD_MKDIR:
            handle_mkdir(task); break;
        case CMD_RMDIR:
            handle_rmdir(task); break;
        case CMD_RM:
            handle_rm(task); break;
        default:
            send_packet(task->client_fd, CMD_ERROR, STATUS_PROTOCOL_ERROR, NULL, 0);
            break;
    }

    // Packet tasks are allocated by the network/thread-pool side for this call.
    free_packet(&task->packet);
    free(task);
}

static int generate_salt(char *salt, size_t salt_size)
{
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";
    unsigned char random_bytes[16];
    int random_fd;
    size_t index;

    if (salt == NULL || salt_size < 20) {
        errno = EINVAL;
        return -1;
    }

    random_fd = open("/dev/urandom", O_RDONLY);
    if (random_fd < 0) {
        return -1;
    }

    if (read(random_fd, random_bytes, sizeof(random_bytes)) !=
        (ssize_t)sizeof(random_bytes)) {
        close(random_fd);
        return -1;
    }

    close(random_fd);

    salt[0] = '$';
    salt[1] = '6';
    salt[2] = '$';
    for (index = 0; index < 16; ++index) {
        salt[3 + index] = charset[random_bytes[index] % (sizeof(charset) - 1)];
    }
    salt[19] = '\0';
    return 0;
}

static void handle_register(packet_task_t *task)
{
    // 1. get the username and password from packet.payload 
    // 2. generate random salt 
    // 3. crypt password with salt to get password_hash
    // 4. insert this new username and password_hash to users, 
    //     also insert a root path record to table paths
    // 5. return CMD_ACK / CMD_ERROR

    char payload[128];
    char* username;
    char* password;
    char* saveptr1;
    char salt[20];

    // 1. get the username and password from packet.payload 
    memcpy(payload, task->packet.payload, task->packet.header.data_len);
    payload[task->packet.header.data_len] = '\0';

    username = strtok_r(payload, "\n", &saveptr1);
    password = strtok_r(NULL, "\n", &saveptr1);

    if (username == NULL || password == NULL) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // 2. generate random salt 
    if (generate_salt(salt, sizeof(salt)) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, NULL, 0);
        return;
    }

    // 3. crypt password with salt to get password_hash
    struct crypt_data data;
    char password_hash[CRYPT_OUTPUT_SIZE] = {0};
    memset(&data, 0, sizeof(data));
    strcpy(data.input, password);
    strcpy(data.setting, salt);
    if (crypt_r(password, salt, &data) == NULL) {
        perror("crypt_r");
        send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, NULL, 0);
        return;
    }
    memcpy(password_hash, data.output, sizeof(data.output));
    memset(&data, 0, sizeof(data));

    // 4. insert this new username and password_hash to users, 
    //     also insert a root path record to table paths
    if (dao_auth_register(task->db_pool, username, password_hash) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, NULL, 0);
        return;
    }

    // 5. return CMD_ACK / CMD_ERROR
    send_packet(task->client_fd, CMD_ACK, STATUS_OK, NULL, 0);
    LOG_INFO("User %s register success", username);
}

static void handle_login(packet_task_t *task)
{
    // 1. get the username and password from packet.payload 
    // 2. query password_hash from table users by username
    // 3. if not exist, return CMD_ERROR, STATUS_USER_NOTEXIST
    // 4. else, strcmp( crypt(password, password_hash), password_hash)
    // 5. set session fields related the logined users
    // 6. return CMD_ACK / CMD_ERROR - STATUS_PASSWD_ERROR

    char payload[128];
    char* username;
    char* password;
    char* saveptr1;
    char *encrypted_password;

    // 1. get the username and password from packet.payload 
    memcpy(payload, task->packet.payload, task->packet.header.data_len);
    payload[task->packet.header.data_len] = '\0';

    username = strtok_r(payload, "\n", &saveptr1);
    password = strtok_r(NULL, "\n", &saveptr1);

    if (username == NULL || password == NULL) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // 2. query password_hash from table users by username
    login_info_t login_info;
    int ret = dao_auth_get_login_info(task->db_pool, username, &login_info);
    if (ret == 1) {
        // username not exist
        // 3. if not exist, return CMD_ERROR, STATUS_USER_NOTEXIST
        send_packet(task->client_fd, CMD_ERROR, STATUS_USER_NOTEXIST, NULL, 0);
        return;
    }
    if (ret == -1) {
        // other error 
        send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, NULL, 0);
        return;
    }

    // 4. else, strcmp( crypt(password, password_hash), password_hash)
    struct crypt_data data;
    memset(&data, 0, sizeof(data));
    strcpy(data.input, password);
    strcpy(data.setting, login_info.password_hash);
    encrypted_password = crypt_r(password, login_info.password_hash, &data);
    if (encrypted_password == NULL || strcmp(login_info.password_hash, encrypted_password) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_PASSWD_ERROR, NULL, 0);
        return;
    }
    memset(&data, 0, sizeof(data));

    // 5. set session fields related the logined users
    if (session_set_login_state(task->client_fd, login_info.user_id, 
                                username, login_info.root_path_id) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, NULL, 0);
        LOG_ERROR("Failed to set login session state");
        return;
    }

    // 6. return CMD_ACK / CMD_ERROR - STATUS_PASSWD_ERROR
    send_packet(task->client_fd, CMD_ACK, STATUS_OK, NULL, 0);
    LOG_INFO("User %s login success", username);
}

static void handle_pwd(packet_task_t* task)
{
    char cwd[PATH_MAX];
    if (session_get_cwd(task->client_fd, cwd, sizeof(cwd)) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, NULL, 0);
        return;
    }
    send_packet(task->client_fd, CMD_ACK, STATUS_OK, cwd, strlen(cwd));
}

static void handle_cd(packet_task_t* task)
{

    // 1. get the path from task->payload
    // 2. get the current `user_id`、`cwd_id`、`cwd` from session
    // 3. normalize user input to new virtual path
    // 4. ensure the objective path exist and is dir from database, and get new path_id
    // 5. update the cwd and cwd_id at this session
    // 6. return ok/error, not a dir or path not exist

    char payload[PATH_MAX];
    char virtual_path[PATH_MAX];
    uint64_t user_id;
    uint64_t cwd_id;
    char cwd[PATH_MAX];
    uint64_t path_id;

    // 1. get the path from task->payload
    if (task->packet.header.data_len == 0) {
        payload[0] = '\0';
    } else {
        memcpy(payload, task->packet.payload, task->packet.header.data_len);
        payload[task->packet.header.data_len] = '\0';
    }

    // 2. get the current `user_id`, `cwd_id`, `cwd` from session
    if (session_get_location(task->client_fd,
                             &user_id,
                             &cwd_id,
                             cwd,
                             sizeof(cwd)) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, NULL, 0);
        return;
    }

    // 3. normalize user input to new virtual path
    // pwd = / , cd .. cd . cd ~ pwd = /
    // pwd = /docs/dir1/dir11 cd .. pwd = /docs/dir1/, cd ../dir12, pwd = /docs/dir12
    // pwd = /docs/dir1 cd /assets/dir1 pwd = /assets/dir1
    // if payload[0] = /, then virtual_path = payload directly.
    // if payload[0] = '\0', then virtual_path = /. (case: cd no-arg)
    if (payload[0] == '\0') {
        strcpy(virtual_path, "/");
    } else if (normalize_cd_path(cwd, payload, virtual_path, sizeof(virtual_path)) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, NULL, 0);
        return;
    }

    // 4. ensure the objective path exist and is dir from database, and get new path_id
    // select id from paths where virtual_path == path and user_id == user_id, 
    int ret = dao_path_get_node_id(task->db_pool, user_id, virtual_path, &path_id);
    switch (ret) {
        case DAO_PATH_IS_DIR:
            break;

        case DAO_PATH_NOT_FOUND:
            send_packet(task->client_fd, CMD_ERROR, STATUS_DIR_NOTEXIST, virtual_path, strlen(virtual_path));
            return;

        case DAO_PATH_IS_FILE:
            send_packet(task->client_fd, CMD_ERROR, STATUS_NOT_DIR, virtual_path, strlen(virtual_path));
            return;

        default:
            send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, NULL, 0);
            return;
    }

    // 5. update the cwd and cwd_id at this session
    if (session_set_cwd(task->client_fd, path_id, virtual_path) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, NULL, 0);
        return;
    }
    // 6. return ok/error, path not exist
    send_packet(task->client_fd, CMD_ACK, STATUS_OK, virtual_path, strlen(virtual_path));
}

static void handle_ls(packet_task_t* task)
{
    // ls no-arg, == ls $(pwd)
    // ls dir/file, if exists, print sub-files with backspace.
    // else, print no such file or directory

    // 1. Get ls argument.
    // 2. Get the current `user_id`、`cwd_id`、`cwd`.
    // 3. Normalize ls target path.
    // 4. Check target path exists.
    // 5. If target path exists and is a file, return file_name.
    // 6. If target path exists and is dir, checkout all child files/dirs.
    // 7. Return success or error

    char payload[PATH_MAX];
    char target_path[PATH_MAX];  // target path
    char parent_path[PATH_MAX];  // the parent of target_path
    char file_name[NAME_MAX];    // base name of target_path
    char* slash;
    uint64_t target_id;
    uint64_t user_id;
    uint64_t cwd_id;
    char cwd[PATH_MAX];
    char ls_resp[MAX_PACKET_PAYLOAD];

    // 1. Get ls argument.
    if (task->packet.header.data_len == 0) {
        payload[0] = '\0';
    } else {
        memcpy(payload, task->packet.payload, task->packet.header.data_len);
        payload[task->packet.header.data_len] = '\0';
    }

    // 2. get the current `user_id`, `cwd_id`, `cwd` from session
    if (session_get_location(task->client_fd,
                             &user_id,
                             &cwd_id,
                             cwd,
                             sizeof(cwd)) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, NULL, 0);
        return;
    }

    // 3. Normalize ls target path.
    if (payload[0] == '\0') {
        strcpy(target_path, cwd);
    } else if (normalize_cd_path(cwd, payload, target_path, sizeof(target_path)) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // 4. Check target path exists.
    int ret = dao_path_get_node_id(task->db_pool, user_id, target_path, &target_id);
    switch (ret) {
        case DAO_PATH_IS_DIR:
        case DAO_PATH_IS_FILE:
            break;

        case DAO_PATH_NOT_FOUND:
            send_packet(task->client_fd, CMD_ERROR, STATUS_DIR_NOTEXIST, target_path, strlen(target_path));
            return;

        default:
            send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, target_path, strlen(target_path));
            return;
    }

    // 5. If target path exists and is a file, return file_name.
    if (ret == DAO_PATH_IS_FILE) {
        // Split target path only after knowing it is a file; root has no basename.
        strncpy(parent_path, target_path, sizeof(parent_path) - 1);
        parent_path[sizeof(parent_path) - 1] = '\0';

        slash = strrchr(parent_path, '/');
        if (slash == NULL) {
            send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
            return;
        }
        strncpy(file_name, slash + 1, sizeof(file_name) - 1);
        file_name[sizeof(file_name) - 1] = '\0';

        if (file_name[0] == '\0') {
            send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
            return;
        }

        send_packet(task->client_fd, CMD_ACK, STATUS_OK, file_name, strlen(file_name));
    }

    // 6. If target path exists and is dir, checkout all child files/dirs.
    if (ret == DAO_PATH_IS_DIR) {
        ls_resp[0] = '\0';
        if (dao_path_get_ls_dir(task->db_pool, user_id, target_id, ls_resp, sizeof(ls_resp)) < 0) {
            send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, target_path, strlen(target_path));
            return;
        }
        send_packet(task->client_fd, CMD_ACK, STATUS_OK, ls_resp, strlen(ls_resp));
    }
}

static void handle_mkdir(packet_task_t* task)
{
    // 1. Get mkdir argument.
    // 2. Get the current `user_id`、`cwd_id`、`cwd`.
    // 3. Normalize mkdir target path.
    // 4. Split target path into parent_path and file_name.
    // 5. Check parent exists and is directory.
    // 6. Insert new directory.
    // 7. Return success or error

    char payload[NAME_MAX];
    char target_path[PATH_MAX];  // target created dir path
    char parent_path[PATH_MAX];  // the parent of target_path
    char file_name[NAME_MAX];    // the target dir name
    char* slash;
    uint64_t user_id;
    uint64_t cwd_id;
    uint64_t parent_id;
    char cwd[PATH_MAX];

    // 1. Get mkdir argument.
    memcpy(payload, task->packet.payload, task->packet.header.data_len);
    payload[task->packet.header.data_len] = '\0';

    // 2. get the current `user_id`, `cwd_id`, `cwd` from session
    if (session_get_location(task->client_fd,
                             &user_id,
                             &cwd_id,
                             cwd,
                             sizeof(cwd)) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, NULL, 0);
        return;
    }

    // 3. Normalize mkdir target path.
    if (normalize_cd_path(cwd, payload, target_path, sizeof(target_path)) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    /*
     * Cannot create virtual root.
     */
    if (strcmp(target_path, "/") == 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    /*
     * 4. Split target path into parent_path and file_name.
     *
     * target_path = "/dir1"        => parent_path = "/", file_name = "dir1"
     * target_path = "/dir1/dir11"  => parent_path = "/dir1", file_name = "dir11"
     */
    strncpy(parent_path, target_path, sizeof(parent_path) - 1);
    parent_path[sizeof(parent_path) - 1] = '\0';

    slash = strrchr(parent_path, '/');
    if (slash == NULL) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }
    strncpy(file_name, slash + 1, sizeof(file_name) - 1);
    file_name[sizeof(file_name) - 1] = '\0';

    if (file_name[0] == '\0') {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    if (slash == parent_path) {
        /*
         * Parent is root.
         */
        parent_path[1] = '\0';
    } else {
        *slash = '\0';
    }

    // 5. Check parent exists and is directory.
    int ret = dao_path_get_node_id(task->db_pool, user_id, parent_path, &parent_id);
    switch (ret) {
        case DAO_PATH_IS_DIR:
            break;

        case DAO_PATH_NOT_FOUND:
            send_packet(task->client_fd, CMD_ERROR, STATUS_DIR_NOTEXIST, parent_path, strlen(parent_path));
            return;

        case DAO_PATH_IS_FILE:
            send_packet(task->client_fd, CMD_ERROR, STATUS_NOT_DIR, parent_path, strlen(parent_path));
            return;

        default:
            send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, parent_path, strlen(parent_path));
            return;
    }

    // 6. Insert new directory.
    ret = dao_path_insert_dir(task->db_pool, user_id, target_path, parent_id, file_name);
    if (ret == 1) {
        // Directory already exists
        send_packet(task->client_fd, CMD_ERROR, STATUS_DIR_ALREADY_EXISTS, file_name, strlen(file_name));
        return; 
    } else if (ret < 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, file_name, strlen(file_name));
        return; 
    }

    // 7. Return success or error
    send_packet(task->client_fd, CMD_ACK, STATUS_OK, file_name, strlen(file_name));
}


static void handle_rmdir(packet_task_t* task)
{
    // 1. Get rmdir argument.
    // 2. Get the current `user_id`、`cwd_id`、`cwd`.
    // 3. Normalize mkdir target path.
    // 4. Check the target path exists and is directory.
    // 5. Check the target path is empty.
    // 6. if empty, delete the record from paths.
    // 7. Return success or error

    char payload[NAME_MAX];
    char target_path[PATH_MAX];  // target created dir path
    char parent_path[PATH_MAX];  // the parent of target_path
    char file_name[NAME_MAX];    // the target dir name
    char* slash;
    uint64_t user_id;
    uint64_t cwd_id;
    char cwd[PATH_MAX];
    uint64_t target_dir_id;

    // 1. Get rmdir argument.
    memcpy(payload, task->packet.payload, task->packet.header.data_len);
    payload[task->packet.header.data_len] = '\0';

    // 2. get the current `user_id`, `cwd_id`, `cwd` from session
    if (session_get_location(task->client_fd,
                             &user_id,
                             &cwd_id,
                             cwd,
                             sizeof(cwd)) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, NULL, 0);
        return;
    }

    // 3. Normalize rmdir target path.
    if (normalize_cd_path(cwd, payload, target_path, sizeof(target_path)) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    /*
     * Cannot rmdir root.
     */
    if (strcmp(target_path, "/") == 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // Split target path into parent_path and file_name.
    strncpy(parent_path, target_path, sizeof(parent_path) - 1);
    parent_path[sizeof(parent_path) - 1] = '\0';

    slash = strrchr(parent_path, '/');
    if (slash == NULL) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }
    strncpy(file_name, slash + 1, sizeof(file_name) - 1);
    file_name[sizeof(file_name) - 1] = '\0';

    if (file_name[0] == '\0') {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    if (slash == parent_path) {
        /*
         * Parent is root.
         */
        parent_path[1] = '\0';
    } else {
        *slash = '\0';
    }

    // 4. Check the target path exists and is directory.
    int ret = dao_path_get_node_id(task->db_pool, user_id, target_path, &target_dir_id);
    switch (ret) {
        case DAO_PATH_IS_DIR:
            break;

        case DAO_PATH_NOT_FOUND:
            send_packet(task->client_fd, CMD_ERROR, STATUS_DIR_NOTEXIST, file_name, strlen(file_name));
            return;

        case DAO_PATH_IS_FILE:
            send_packet(task->client_fd, CMD_ERROR, STATUS_NOT_DIR, file_name, strlen(file_name));
            return;

        default:
            send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, file_name, strlen(file_name));
            return;
    }

    // 5. Check the target dir is empty.(parent_id == path_id)
    ret = dao_path_dir_has_child(task->db_pool, user_id, target_dir_id); 
    if (ret > 0) {
        // dir is not empty
        send_packet(task->client_fd, CMD_ERROR, STATUS_DIR_NOTEMPTY, file_name, strlen(file_name));
        return;
    } else if (ret < 0) {
        // db error
        send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, file_name, strlen(file_name));
        return;
    }

    // ret == 0
    // 6. if empty, delete the record from paths.
    if (dao_path_rmdir(task->db_pool, target_dir_id) != 0) {
        // db error
        send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, file_name, strlen(file_name));
        return;
    }

    // 7. Return success or error
    send_packet(task->client_fd, CMD_ACK, STATUS_OK, target_path, strlen(target_path));
}

static void handle_rm(packet_task_t* task)
{
    char payload[NAME_MAX];
    char cwd[PATH_MAX];
    char target_path[PATH_MAX];
    char temp_target_path[PATH_MAX];
    char* base_file_name;
    char hashed_filename[NAME_MAX];
    char physical_file_path[PATH_MAX + NAME_MAX + 1];

    uint64_t user_id;
    uint64_t cwd_id;
    uint64_t target_file_id;
    int ret;

    // 1. Read the rm argument from the packet payload.
    memcpy(payload, task->packet.payload, task->packet.header.data_len);
    payload[task->packet.header.data_len] = '\0';

    // 2. Resolve the current session location.
    if (session_get_location(task->client_fd,
                             &user_id,
                             &cwd_id,
                             cwd,
                             sizeof(cwd)) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, NULL, 0);
        return;
    }

    // 3. Normalize the target path against cwd.
    if (normalize_cd_path(cwd, payload, target_path, sizeof(target_path)) != 0) {
        send_packet(task->client_fd, CMD_ERROR, STATUS_BAD_REQUEST, NULL, 0);
        return;
    }

    // get base file name of target path.
    strncpy(temp_target_path, target_path, sizeof(temp_target_path) - 1);
    temp_target_path[sizeof(temp_target_path) - 1] = '\0';
    base_file_name = basename(temp_target_path);

    // 4. The rm target must exist and must be a file.
    ret = dao_path_get_node_id(task->db_pool, user_id, target_path, &target_file_id);
    switch (ret) {
        case DAO_PATH_IS_FILE:
            break;

        case DAO_PATH_IS_DIR:
            send_packet(task->client_fd, CMD_ERROR, STATUS_IS_DIR, base_file_name, strlen(base_file_name));
            return;

        case DAO_PATH_NOT_FOUND:
            send_packet(task->client_fd, CMD_ERROR, STATUS_FILE_NOTEXIST, base_file_name, strlen(base_file_name));
            return;

        default:
            send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, base_file_name, strlen(base_file_name));
            return;
    }

    // 5. Remove the path record and decrement the physical file reference.
    ret = dao_rm_file(task->db_pool, user_id, target_file_id, hashed_filename, sizeof(hashed_filename));
    switch (ret) {
        case DAO_OK:
            break;

        case DAO_NOT_FOUND:
            send_packet(task->client_fd, CMD_ERROR, STATUS_FILE_NOTEXIST, base_file_name, strlen(base_file_name));
            return;

        case DAO_SHOULD_DELETE_PHYSICAL:
            // Last reference: delete storage_root/<hashed_filename> from disk.
            snprintf(physical_file_path, sizeof(physical_file_path), "%s/%s",
                     task->storage_root, hashed_filename);
            if (unlink(physical_file_path) != 0) {
                LOG_ERROR("Failed to unlink '%s': %s", base_file_name, strerror(errno));
                send_packet(task->client_fd, CMD_ERROR, STATUS_IO_ERROR, base_file_name, strlen(base_file_name));
            }
            break;

        case DAO_DB_ERROR:
            send_packet(task->client_fd, CMD_ERROR, STATUS_DB_ERROR, NULL, 0);
            return;

        default:
            send_packet(task->client_fd, CMD_ERROR, STATUS_OTHER_ERROR, base_file_name, strlen(base_file_name));
            return;
    }

    // 6. Report success after both metadata and optional disk cleanup finish.
    send_packet(task->client_fd, CMD_ACK, STATUS_OK, base_file_name, strlen(base_file_name));
}
