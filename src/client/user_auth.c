#include "client/user_auth.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "client/state.h"
#include "common/utils.h"
#include "common/protocol.h"
#include "common/tui.h"

static void tui_draw_menu_border(void)
{
    printf(COLOR_BLUE "+------------------------------------------------+\n"
           COLOR_RESET);
}

static void tui_draw_menu_item(const char *item)
{
    printf(COLOR_BLUE "| " COLOR_RESET 
           "                  %-28s"
           COLOR_BLUE " |\n" COLOR_RESET,
           item);
}

static int menu_show_auth(void)
{
    int choice;
    char str_choice[16];

    printf(COLOR_BLUE "==================================================\n");
    printf("|" COLOR_CYAN "             Cloud Drive Demo Client            "
           COLOR_BLUE "|\n");
    printf("==================================================\n" COLOR_RESET);
    tui_draw_menu_border();
    tui_draw_menu_item("1. Login");
    tui_draw_menu_item("2. Register");
    tui_draw_menu_item("0. Exit");
    tui_draw_menu_border();

    printf("Please choose: ");
    if (s_gets(str_choice, sizeof(str_choice)) == NULL) {
        return -1;
    }
    choice = strtol(str_choice, NULL, 10);

    return choice;
}

void print_command_menu()
{
    printf(COLOR_BLUE "\n+------------------------------------------------+\n");
    printf("|" COLOR_CYAN "              Cloud Drive Commands             "
           COLOR_BLUE "|\n");
    printf("+------------------------------------------------+\n" COLOR_RESET);
    printf(COLOR_BLUE "| " COLOR_YELLOW "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "pwd", "show current remote path");
    printf(COLOR_BLUE "| " COLOR_YELLOW "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "cd <dir>", "change remote directory");
    printf(COLOR_BLUE "| " COLOR_YELLOW "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "ls", "list remote files");
    printf(COLOR_BLUE "| " COLOR_YELLOW "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "ll", "list remote files in detail");
    printf(COLOR_BLUE "| " COLOR_YELLOW "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "tree", "show remote directory tree");
    printf(COLOR_BLUE "| " COLOR_YELLOW "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "mkdir <dir>", "create remote directory");
    printf(COLOR_BLUE "| " COLOR_YELLOW "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "rmdir <dir>", "remove remote directory");
    printf(COLOR_BLUE "| " COLOR_YELLOW "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "rm <file>", "remove remote file");
    printf(COLOR_BLUE "| " COLOR_YELLOW "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "cat <file>", "print remote file");
    printf(COLOR_BLUE "| " COLOR_YELLOW "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "upload <local>", "upload local file");
    printf(COLOR_BLUE "| " COLOR_YELLOW "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "download <r>", "download remote file");
    printf(COLOR_BLUE "+------------------------------------------------+\n");
    printf("| " COLOR_GREEN "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "help", "show this command menu");
    printf(COLOR_BLUE "| " COLOR_GREEN "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "logout", "sign out current user");
    printf(COLOR_BLUE "| " COLOR_GREEN "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "quit", "exit cloud drive client");
    printf(COLOR_BLUE "+------------------------------------------------+\n\n" COLOR_RESET);
}

int user_auth(client_state_t *client_state)
{
    while (1) {

        int choice = menu_show_auth();

        switch (choice) {
            case 1:
                if (user_login(client_state) == 0) {
                    return AUTH_OK;
                }
                printf("Login failed, please try again\n");
                break;
            case 2:
                if (user_register(client_state) == 0) {
                    printf("Register success, please login\n");
                } else {
                    printf("Register failed\n");
                }
                break;

            case 0:
                return AUTH_EXIT;

            default:
                printf("Invalid choice\n");
                break;
        }
    }
}

// TODO: safe register and login with TLS.
int user_login(client_state_t *state)
{
    // 1. get username and password, 
    // 2. send username + \n + password to server with CMD_LOGIN_REQ
    // 3. recv server respnse
    // 4. if CMD_ACK, return 0, otherwise, return -1 

    char username[64];
    char password[32];
    char payload[128];
    packet_t packet;

    // 1. get username and password, 
    printf("Please enter your username: ");
    if (s_gets(username, sizeof(username)) == NULL) {
        return -1;
    }
    printf("Please enter your password: ");
    if (read_password(password, sizeof(password)) != 0) {
        return -1;
    }

    // Now use plaintext password to transfer temporarily,
    // use encrypted_hash still not safe.

    // 2. send username + \n + password to server with CMD_LOGIN_REQ
    if (snprintf(payload, sizeof(payload), "%s\n%s", username, password) < 0) {
        return -1;
    }
    memset(&packet, 0, sizeof(packet));
    if (send_packet(state->sock_fd, CMD_LOGIN_REQ, STATUS_OK, payload, strlen(payload)) < 0) {
        return -1;
    }
    // 3. recv server respnse
    if (recv_packet(state->sock_fd, &packet) < 0) {
        return -1;
    }
    // 4. if CMD_ACK, return 0, otherwise, return -1 
    if (packet.header.cmd_type == CMD_ACK) {
        // login success
        // update client state
        state->status = CLIENT_STATE_CONNECTED;
        strcpy(state->username, username);
        strcpy(state->remote_cwd, "/");
        return 0;
    } else if (packet.header.cmd_type == CMD_ERROR) {
        if (packet.header.status == STATUS_USER_NOTEXIST) {
            printf("Username %s not exist\n", username);
        }
        if (packet.header.status == STATUS_PASSWD_ERROR) {
            printf("Password is not correct\n");
        }
    }
        return -1;
}

int user_register(client_state_t *state)
{
    // 1. get username and password, and confirm password 
    // 2. send username + \n + password to server with CMD_REGISTER_REQ
    // 3. recv server respnse
    // 4. if CMD_ACK, return 0, otherwise, return -1 

    char username[64];
    char password[32];
    char confirm_password[32];
    char payload[128];
    packet_t packet;

    // 1. get username and password, 
    printf("Please enter your username: ");
    if (s_gets(username, sizeof(username)) == NULL) {
        return -1;
    }
    enter_password:
    printf("Please enter your password: ");
    if (read_password(password, sizeof(password)) != 0) {
        return -1;
    }
    printf("Please enter your password again: ");
    if (read_password(confirm_password, sizeof(confirm_password)) != 0) {
        return -1;
    }
    if (strcmp(password, confirm_password) != 0) {
        printf("The passwords you entered do not match.\n\n");
        goto enter_password; 
    }

    // Now use plaintext password to transfer temporarily,
    // use encrypted_hash still not safe.

    // 2. send username + \n + password to server with CMD_REGISTER_REQ
    if (snprintf(payload, sizeof(payload), "%s\n%s", username, password) < 0) {
        return -1;
    }
    memset(&packet, 0, sizeof(packet));
    if (send_packet(state->sock_fd, CMD_REGISTER_REQ, STATUS_OK, payload, strlen(payload)) < 0) {
        return -1;
    }
    // 3. recv server respnse
    if (recv_packet(state->sock_fd, &packet) < 0) {
        return -1;
    }
    // 4. if CMD_ACK, return 0, otherwise, return -1 
    if (packet.header.cmd_type == CMD_ACK) {
        return 0;
    } else {
        return -1;
    }
}
