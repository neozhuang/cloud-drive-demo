#include "client/user_auth.h"

#include <stdio.h>
#include <string.h>

#include "client/menu.h"
#include "client/connection.h"
#include "common/protocol.h"
#include "common/utils.h"

enum {
    AUTH_REQUEST_FAILED = -1,
    AUTH_REQUEST_CONNECTION_ERROR = -2
};

static int exchange_auth(client_runtime_t *runtime, cmd_type_t type,
                         const char *payload, packet_t *response)
{
    packet_t request;

    if (packet_init(&request, type, STATUS_OK, NULL,
                    payload, (uint32_t)strlen(payload)) != 0) {
        return -1;
    }
    if (protocol_send_packet(runtime->control_fd, &request) != 0) {
        return -1;
    }
    return protocol_recv_packet(runtime->control_fd, response);
}

static int read_credentials(char *username, size_t username_size,
                            char *password, size_t password_size)
{
    printf("Please enter your username: ");
    if (s_gets(username, (int)username_size) == NULL || username[0] == '\0') {
        return -1;
    }
    printf("Please enter your password: ");
    return read_password(password, password_size);
}

int user_auth(client_runtime_t *runtime)
{
    while (1) {
        switch (menu_show_auth()) {
            case 1: {
                if (!client_connection_is_alive(runtime->control_fd)) {
                    return AUTH_CONNECTION_ERROR;
                }
                int result = user_login(runtime);
                if (result == 0) {
                    return AUTH_OK;
                }
                if (result == AUTH_REQUEST_CONNECTION_ERROR) {
                    return AUTH_CONNECTION_ERROR;
                }
                printf("Login failed, please try again\n");
                break;
            }
            case 2: {
                if (!client_connection_is_alive(runtime->control_fd)) {
                    return AUTH_CONNECTION_ERROR;
                }
                int result = user_register(runtime);
                if (result == 0) {
                    printf("Register success, please login\n");
                } else {
                    if (result == AUTH_REQUEST_CONNECTION_ERROR) {
                        return AUTH_CONNECTION_ERROR;
                    }
                    printf("Register failed\n");
                }
                break;
            }
            case 0:
                return AUTH_EXIT;
            default:
                printf("Invalid choice\n");
                break;
        }
    }
}

int user_login(client_runtime_t *runtime)
{
    char username[64];
    char password[32];
    char payload[128];
    packet_t response = {0};
    int result = AUTH_REQUEST_FAILED;

    memset(password, 0, sizeof(password));
    client_runtime_clear_session(runtime);
    if (read_credentials(username, sizeof(username), password,
                         sizeof(password)) != 0) {
        goto out;
    }

    snprintf(payload, sizeof(payload), "%s\n%s", username, password);
    if (exchange_auth(runtime, CMD_LOGIN_REQ, payload, &response) != 0) {
        result = AUTH_REQUEST_CONNECTION_ERROR;
        goto out;
    }

    if (response.header.type == CMD_ACK &&
        response.header.status == STATUS_OK &&
        !session_id_is_empty(&response.header.session_id)) {
        result = client_runtime_publish_session(runtime,
                                                &response.header.session_id,
                                                username);
    } else if (response.header.type == CMD_ERROR) {
        if (response.header.status == STATUS_USER_NOTEXIST) {
            printf("Username %s does not exist\n", username);
        } else if (response.header.status == STATUS_PASSWD_ERROR) {
            printf("Password is not correct\n");
        }
    }

out:
    memset(password, 0, sizeof(password));
    packet_release(&response);
    return result;
}

int user_register(client_runtime_t *runtime)
{
    char username[64];
    char password[32];
    char confirm_password[32];
    char payload[128];
    packet_t response = {0};
    int result = AUTH_REQUEST_FAILED;

    memset(password, 0, sizeof(password));
    memset(confirm_password, 0, sizeof(confirm_password));

    printf("Please enter your username: ");
    if (s_gets(username, sizeof(username)) == NULL || username[0] == '\0') {
        goto out;
    }
    while (1) {
        printf("Please enter your password: ");
        if (read_password(password, sizeof(password)) != 0) {
            goto out;
        }
        printf("Please enter your password again: ");
        if (read_password(confirm_password, sizeof(confirm_password)) != 0) {
            goto out;
        }
        if (strcmp(password, confirm_password) == 0) {
            break;
        }
        printf("The passwords you entered do not match.\n\n");
    }

    snprintf(payload, sizeof(payload), "%s\n%s", username, password);
    if (exchange_auth(runtime, CMD_REGISTER_REQ, payload, &response) != 0) {
        result = AUTH_REQUEST_CONNECTION_ERROR;
        goto out;
    }
    if (response.header.type == CMD_ACK &&
        response.header.status == STATUS_OK &&
        session_id_is_empty(&response.header.session_id)) {
        result = 0;
    }

out:
    memset(password, 0, sizeof(password));
    memset(confirm_password, 0, sizeof(confirm_password));
    packet_release(&response);
    return result;
}
