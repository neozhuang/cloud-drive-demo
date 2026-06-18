#include "client/auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/state.h"
#include "common/utils.h"
#include "common/protocol.h"

int user_login(client_state_t *state)
{
    char username[64];
    packet_t packet;
    while (1) {
        memset(&packet, 0, sizeof(packet));
        memset(username, 0, sizeof(username));
        printf("Please enter your username (empty line to quit): ");
        fflush(stdout);
        if (s_gets(username, sizeof(username)) == NULL) {
            return -1;
        }

        // empty line
        if (username[0] == '\0') {
            exit(0);
        }

        if (send_packet(state->sock_fd, CMD_LOGIN_REQ, STATUS_OK, username, strlen(username)) < 0) {
            return -1;
        }
        // recieve respnse, 
        if (recv_packet(state->sock_fd, &packet) < 0) {
            return -1;
        }
        if (packet.header.cmd_type == CMD_ACK) {
            // login success
            // update client state
            state->status = CLIENT_STATE_CONNECTED;
            strcpy(state->username, username);
            strcpy(state->remote_cwd, "/");
            return 0;
        } else {
            printf("username is not correct.\n");
            continue;
        }
    }
}
