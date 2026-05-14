// auth_handler.c --- handle user authentication related requests
// In the version 3 of the cloud drive demo, 
// we implement user authentication module with a MySQL database api query mode
// rather than STMT mode.
#include "service/auth_service.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mysql/mysqld_error.h>

#include "session/session.h"
#include "transport/packet_io.h"

#define PASSWORD_SALT_RANDOM_LEN 16
#define PASSWORD_SALT_LEN (3 + PASSWORD_SALT_RANDOM_LEN)
#define USERNAME_MAX_LEN 64
#define PASSWORD_HASH_MAX_LEN 255
#define AUTH_QUERY_MAX_LEN 1024

typedef struct AuthCredentials {
    char username[USERNAME_MAX_LEN + 1];
    char password_hash[PASSWORD_HASH_MAX_LEN + 1];
    size_t username_length;
    size_t password_hash_length;
} AuthCredentials;

typedef struct AuthUser {
    unsigned long long id;
    char password_hash[PASSWORD_HASH_MAX_LEN + 1];
} AuthUser;

static _Thread_local char g_auth_response_payload[PASSWORD_HASH_MAX_LEN + 1];

static void set_auth_response(uint16_t *status, const char **message,
                              uint16_t response_status,
                              const char *response_message)
{
    if (status != NULL) {
        *status = response_status;
    }
    if (message != NULL) {
        *message = response_message;
    }
}

static int validate_auth_handler_arguments(const PacketTaskContext *context,
                                           MYSQL *mysql, uint16_t *status,
                                           const char **message,
                                           const char *invalid_message)
{
    if (status == NULL || message == NULL) {
        errno = EINVAL;
        return -1;
    }

    set_auth_response(status, message, PROTOCOL_STATUS_INTERNAL_ERROR,
                      invalid_message);
    if (context == NULL || mysql == NULL) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

static int copy_payload_string(const ProtocolPacket *packet, char *buffer,
                               size_t buffer_size)
{
    size_t payload_length;

    if (packet == NULL || buffer == NULL || buffer_size == 0) {
        errno = EINVAL;
        return -1;
    }

    payload_length = (size_t)packet->payload_length;
    if (payload_length == 0 || payload_length >= buffer_size ||
        memchr(packet->payload, '\0', payload_length) != NULL) {
        errno = EINVAL;
        return -1;
    }

    memcpy(buffer, packet->payload, payload_length);
    buffer[payload_length] = '\0';
    return 0;
}

static int parse_credentials_payload(const ProtocolPacket *packet,
                                     AuthCredentials *credentials)
{
    const unsigned char *separator;

    if (packet == NULL || credentials == NULL) {
        errno = EINVAL;
        return -1;
    }

    separator = memchr(packet->payload, '\n', packet->payload_length);
    if (separator == NULL) {
        errno = EINVAL;
        return -1;
    }

    credentials->username_length = (size_t)(separator - packet->payload);
    credentials->password_hash_length =
        (size_t)packet->payload_length - credentials->username_length - 1;
    if (credentials->username_length == 0 ||
        credentials->username_length > USERNAME_MAX_LEN ||
        credentials->password_hash_length == 0 ||
        credentials->password_hash_length > PASSWORD_HASH_MAX_LEN ||
        memchr(separator + 1, '\0', credentials->password_hash_length) != NULL) {
        errno = EINVAL;
        return -1;
    }

    memcpy(credentials->username, packet->payload, credentials->username_length);
    credentials->username[credentials->username_length] = '\0';
    memcpy(credentials->password_hash, separator + 1,
           credentials->password_hash_length);
    credentials->password_hash[credentials->password_hash_length] = '\0';
    return 0;
}

static int escape_mysql_string(MYSQL *mysql, const char *source,
                               size_t source_length, char *escaped,
                               size_t escaped_size)
{
    unsigned long escaped_length;

    if (mysql == NULL || source == NULL || escaped == NULL ||
        escaped_size < source_length * 2 + 1) {
        errno = EINVAL;
        return -1;
    }

    escaped_length = mysql_real_escape_string(mysql, escaped, source,
                                             (unsigned long)source_length);
    escaped[escaped_length] = '\0';
    return 0;
}

static int find_active_user_by_username(MYSQL *mysql, const char *username,
                                        AuthUser *user, int *found)
{
    char escaped_username[USERNAME_MAX_LEN * 2 + 1];
    char query[AUTH_QUERY_MAX_LEN];
    int written_length;
    MYSQL_RES *result;
    MYSQL_ROW row;

    if (mysql == NULL || username == NULL || found == NULL) {
        errno = EINVAL;
        return -1;
    }

    *found = 0;
    if (escape_mysql_string(mysql, username, strlen(username), escaped_username,
                            sizeof(escaped_username)) != 0) {
        return -1;
    }

    written_length = snprintf(
        query, sizeof(query),
        "SELECT id, password_hash FROM users WHERE username = '%s' "
        "AND is_deleted = 0 LIMIT 1",
        escaped_username);
    if (written_length < 0 || (size_t)written_length >= sizeof(query)) {
        errno = EMSGSIZE;
        return -1;
    }

    if (mysql_query(mysql, query) != 0) {
        fprintf(stderr, "Failed to query user '%s': %s\n", username,
                mysql_error(mysql));
        return -1;
    }

    result = mysql_store_result(mysql);
    if (result == NULL) {
        fprintf(stderr, "Failed to read user query result: %s\n",
                mysql_error(mysql));
        return -1;
    }

    row = mysql_fetch_row(result);
    if (row != NULL) {
        *found = 1;
        if (user != NULL) {
            user->id = strtoull(row[0], NULL, 10);
            snprintf(user->password_hash, sizeof(user->password_hash), "%s",
                     row[1] == NULL ? "" : row[1]);
        }
    }

    mysql_free_result(result);
    return 0;
}

static int username_exists(MYSQL *mysql, const char *username, int *exists)
{
    char escaped_username[USERNAME_MAX_LEN * 2 + 1];
    char query[AUTH_QUERY_MAX_LEN];
    int written_length;
    MYSQL_RES *result;

    if (mysql == NULL || username == NULL || exists == NULL) {
        errno = EINVAL;
        return -1;
    }

    *exists = 0;
    if (escape_mysql_string(mysql, username, strlen(username), escaped_username,
                            sizeof(escaped_username)) != 0) {
        return -1;
    }

    written_length = snprintf(query, sizeof(query),
                              "SELECT id FROM users WHERE username = '%s' LIMIT 1",
                              escaped_username);
    if (written_length < 0 || (size_t)written_length >= sizeof(query)) {
        errno = EMSGSIZE;
        return -1;
    }

    if (mysql_query(mysql, query) != 0) {
        fprintf(stderr, "Failed to query user '%s': %s\n", username,
                mysql_error(mysql));
        return -1;
    }

    result = mysql_store_result(mysql);
    if (result == NULL) {
        fprintf(stderr, "Failed to read username query result: %s\n",
                mysql_error(mysql));
        return -1;
    }

    *exists = mysql_num_rows(result) > 0;
    mysql_free_result(result);
    return 0;
}

static int insert_user(MYSQL *mysql, const AuthCredentials *credentials)
{
    char escaped_username[USERNAME_MAX_LEN * 2 + 1];
    char escaped_password_hash[PASSWORD_HASH_MAX_LEN * 2 + 1];
    char query[AUTH_QUERY_MAX_LEN];
    int written_length;

    if (mysql == NULL || credentials == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (escape_mysql_string(mysql, credentials->username,
                            credentials->username_length, escaped_username,
                            sizeof(escaped_username)) != 0 ||
        escape_mysql_string(mysql, credentials->password_hash,
                            credentials->password_hash_length,
                            escaped_password_hash,
                            sizeof(escaped_password_hash)) != 0) {
        return -1;
    }

    written_length = snprintf(
        query, sizeof(query),
        "INSERT INTO users (username, password_hash) VALUES ('%s', '%s')",
        escaped_username, escaped_password_hash);
    if (written_length < 0 || (size_t)written_length >= sizeof(query)) {
        errno = EMSGSIZE;
        return -1;
    }

    if (mysql_query(mysql, query) != 0) {
        fprintf(stderr, "Failed to insert user '%s': %s\n", credentials->username,
                mysql_error(mysql));
        return -1;
    }

    return 0;
}

static int mark_user_deleted(MYSQL *mysql, unsigned long long user_id)
{
    char query[AUTH_QUERY_MAX_LEN];
    int written_length;

    if (mysql == NULL || user_id == 0) {
        errno = EINVAL;
        return -1;
    }

    written_length = snprintf(query, sizeof(query),
                              "UPDATE users SET is_deleted = 1 WHERE id = %llu",
                              user_id);
    if (written_length < 0 || (size_t)written_length >= sizeof(query)) {
        errno = EMSGSIZE;
        return -1;
    }

    if (mysql_query(mysql, query) != 0) {
        fprintf(stderr, "Failed to delete user %llu: %s\n", user_id,
                mysql_error(mysql));
        return -1;
    }

    return 0;
}

static int set_session_user(int client_fd, unsigned long long user_id)
{
    Session *session = session_find(client_fd);

    if (session == NULL || user_id > UINT64_MAX) {
        errno = EINVAL;
        return -1;
    }

    return session_set_user(session, (uint64_t)user_id);
}

static int write_response_payload(const char *payload)
{
    size_t payload_length;

    if (payload == NULL) {
        errno = EINVAL;
        return -1;
    }

    payload_length = strlen(payload);
    if (payload_length >= sizeof(g_auth_response_payload)) {
        errno = EMSGSIZE;
        return -1;
    }

    memcpy(g_auth_response_payload, payload, payload_length + 1);
    return 0;
}

static int generate_password_salt(char *salt, size_t salt_size)
{
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";
    unsigned char random_bytes[PASSWORD_SALT_RANDOM_LEN];
    int random_fd;
    size_t index;

    if (salt == NULL || salt_size < PASSWORD_SALT_LEN + 1) {
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
    for (index = 0; index < PASSWORD_SALT_RANDOM_LEN; ++index) {
        salt[3 + index] = charset[random_bytes[index] % (sizeof(charset) - 1)];
    }
    salt[PASSWORD_SALT_LEN] = '\0';
    return 0;
}

static int send_salt_packet(int client_fd, uint16_t type, const char *salt)
{
    ProtocolPacket packet;
    size_t salt_length;

    if (salt == NULL) {
        errno = EINVAL;
        return -1;
    }

    salt_length = strlen(salt);

    memset(&packet, 0, sizeof(packet));
    packet.type = type;
    packet.status = PROTOCOL_STATUS_OK;
    packet.payload_length = (uint32_t)salt_length;
    memcpy(packet.payload, salt, salt_length);
    return send_packet(client_fd, &packet) < 0 ? -1 : 0;
}

int auth_handle_register_request(const PacketTaskContext *context, MYSQL *mysql)
{
    // recv register request with username
    char salt[PASSWORD_SALT_LEN + 1];
    char username[USERNAME_MAX_LEN + 1];

    strcpy(username, (char *)context->packet.payload);
    // query user by username, if not exist, generate salt and send back to client

    

    MYSQL_RES *result = mysql_store_result(mysql);
    if (result == NULL) {
        fprintf(stderr, "Failed to query user '%s': %s\n", username,
                mysql_error(mysql));
        return -1;
    }

    if (mysql_num_rows(result) > 0) {
        fprintf(stderr, "User '%s' already exists\n", username);
        mysql_free_result(result);
        return -1;
    }
    mysql_free_result(result);

    if (generate_password_salt(salt, sizeof(salt)) != 0) {
        perror("Failed to generate random salt");
        return -1;
    }

    if (send_salt_packet(context->client_fd, context->packet.type, salt) != 0) {
        perror("Failed to send salt packet");
        return -1;
    }

    return 0;
}

void auth_handle_register(const PacketTaskContext *context, MYSQL *mysql,
                                 uint16_t *status, const char **message)
{
    AuthCredentials credentials;
    int exists;

    if (validate_auth_handler_arguments(context, mysql, status, message,
                                        "invalid register handler arguments") != 0) {
        return;
    }

    if (parse_credentials_payload(&context->packet, &credentials) != 0) {
        *message = "invalid register packet payload";
        return;
    }

    if (username_exists(mysql, credentials.username, &exists) != 0) {
        *message = "failed to query user";
        return;
    }

    if (exists) {
        *status = PROTOCOL_STATUS_USERNAME_ALREADY_EXISTS;
        *message = "username already exists";
        return;
    }

    if (insert_user(mysql, &credentials) != 0) {
        if (mysql_errno(mysql) == ER_DUP_ENTRY) {
            *status = PROTOCOL_STATUS_USERNAME_ALREADY_EXISTS;
            *message = "username already exists";
            return;
        }
        *message = "failed to create user";
        return;
    }

    *status = PROTOCOL_STATUS_OK;
    *message = "register success";
}

void auth_handle_login_request(const PacketTaskContext *context, MYSQL *mysql,
                                      uint16_t *status, const char **message)
{
    char username[USERNAME_MAX_LEN + 1];
    AuthUser user;
    int found;

    if (validate_auth_handler_arguments(context, mysql, status, message,
                                        "invalid login request handler arguments") != 0) {
        return;
    }

    if (copy_payload_string(&context->packet, username, sizeof(username)) != 0) {
        *message = "invalid login request payload";
        return;
    }

    if (find_active_user_by_username(mysql, username, &user, &found) != 0) {
        *message = "failed to query user";
        return;
    }

    if (!found) {
        *status = PROTOCOL_STATUS_USERNAME_NOT_EXIST;
        *message = "username not exist";
        return;
    }

    if (write_response_payload(user.password_hash) != 0) {
        *message = "failed to prepare password salt";
        return;
    }

    *status = PROTOCOL_STATUS_OK;
    *message = g_auth_response_payload;
}

void auth_handle_login(const PacketTaskContext *context, MYSQL *mysql,
                              uint16_t *status, const char **message)
{
    AuthCredentials credentials;
    AuthUser user;
    int found;

    if (validate_auth_handler_arguments(context, mysql, status, message,
                                        "invalid login handler arguments") != 0) {
        return;
    }

    if (parse_credentials_payload(&context->packet, &credentials) != 0) {
        *message = "invalid login packet payload";
        return;
    }

    if (find_active_user_by_username(mysql, credentials.username, &user, &found) !=
        0) {
        *message = "failed to query user";
        return;
    }

    if (!found) {
        *status = PROTOCOL_STATUS_USERNAME_NOT_EXIST;
        *message = "username not exist";
        return;
    }

    if (strcmp(credentials.password_hash, user.password_hash) != 0) {
        *status = PROTOCOL_STATUS_PASSWORD_INCORRECT;
        *message = "password error";
        return;
    }

    if (set_session_user(context->client_fd, user.id) != 0) {
        *message = "failed to update session";
        return;
    }

    *status = PROTOCOL_STATUS_OK;
    *message = "login success";
}

void auth_handle_delete_request(const PacketTaskContext *context, MYSQL *mysql,
                                       uint16_t *status, const char **message)
{
    char username[USERNAME_MAX_LEN + 1];
    AuthUser user;
    int found;

    if (validate_auth_handler_arguments(context, mysql, status, message,
                                        "invalid delete request handler arguments") != 0) {
        return;
    }

    if (copy_payload_string(&context->packet, username, sizeof(username)) != 0) {
        *message = "invalid delete request payload";
        return;
    }

    if (find_active_user_by_username(mysql, username, &user, &found) != 0) {
        *message = "failed to query user";
        return;
    }

    if (!found) {
        *status = PROTOCOL_STATUS_USERNAME_NOT_EXIST;
        *message = "username not exist";
        return;
    }

    if (write_response_payload(user.password_hash) != 0) {
        *message = "failed to prepare password salt";
        return;
    }

    *status = PROTOCOL_STATUS_OK;
    *message = g_auth_response_payload;
}

void auth_handle_delete(const PacketTaskContext *context, MYSQL *mysql,
                               uint16_t *status, const char **message)
{
    AuthCredentials credentials;
    AuthUser user;
    int found;

    if (validate_auth_handler_arguments(context, mysql, status, message,
                                        "invalid delete handler arguments") != 0) {
        return;
    }

    if (parse_credentials_payload(&context->packet, &credentials) != 0) {
        *message = "invalid delete packet payload";
        return;
    }

    if (find_active_user_by_username(mysql, credentials.username, &user, &found) !=
        0) {
        *message = "failed to query user";
        return;
    }

    if (!found) {
        *status = PROTOCOL_STATUS_USERNAME_NOT_EXIST;
        *message = "username not exist";
        return;
    }

    if (strcmp(credentials.password_hash, user.password_hash) != 0) {
        *status = PROTOCOL_STATUS_PASSWORD_INCORRECT;
        *message = "password error";
        return;
    }

    if (mark_user_deleted(mysql, user.id) != 0) {
        *message = "failed to delete user";
        return;
    }

    if (set_session_user(context->client_fd, 0) != 0) {
        *message = "failed to update session";
        return;
    }

    *status = PROTOCOL_STATUS_OK;
    *message = "delete success";
}

void auth_handle_logout(const PacketTaskContext *context, MYSQL *mysql,
                               uint16_t *status, const char **message)
{
    (void)mysql;

    if (context == NULL || status == NULL || message == NULL) {
        errno = EINVAL;
        return;
    }

    *status = PROTOCOL_STATUS_INTERNAL_ERROR;
    *message = "invalid logout handler arguments";

    if (set_session_user(context->client_fd, 0) != 0) {
        *message = "failed to update session";
        return;
    }

    *status = PROTOCOL_STATUS_OK;
    *message = "logout success";
}
