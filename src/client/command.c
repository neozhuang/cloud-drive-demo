#include "client/command.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "client/menu.h"
#include "client/connection.h"
#include "client/transfer.h"
#include "common/log.h"
#include "common/protocol.h"

static const char *skip_space(const char *text)
{
    while (*text != '\0' && isspace((unsigned char)*text)) {
        ++text;
    }
    return text;
}

static int next_word(const char **cursor, char *word, size_t word_size)
{
    const char *start = skip_space(*cursor);
    const char *end = start;
    size_t length;

    while (*end != '\0' && !isspace((unsigned char)*end)) {
        ++end;
    }
    length = (size_t)(end - start);
    if (length == 0 || length >= word_size) {
        return -1;
    }
    memcpy(word, start, length);
    word[length] = '\0';
    *cursor = end;
    return 0;
}

static client_command_result_t control_connection_lost(
    client_runtime_t *runtime, cmd_type_t type)
{
    LOG_ERROR("control connection lost while executing %s",
              cmd_type_to_str(type));
    client_runtime_disconnect_control(runtime);
    return CLIENT_COMMAND_RECONNECT;
}

static client_command_result_t execute_short_command(
    client_runtime_t *runtime, cmd_type_t type, const char *argument)
{
    session_id_t session_id;
    packet_t request;
    packet_t response = {0};
    char username[64];
    char cwd[PATH_MAX];
    char text[MAX_PACKET_PAYLOAD + 1U];
    client_command_result_t result = CLIENT_COMMAND_ERROR;

    if (client_runtime_session_snapshot(runtime, &session_id,
                                         username, sizeof(username),
                                         cwd, sizeof(cwd)) != 0 ||
        packet_init(&request, type, STATUS_OK, &session_id, argument,
                     (uint32_t)strlen(argument)) != 0) {
        goto out;
    }
    if (protocol_send_packet(runtime->control_fd, &request) != 0 ||
        protocol_recv_packet(runtime->control_fd, &response) != 0) {
        result = control_connection_lost(runtime, type);
        goto out;
    }
    if (!session_id_equal(&session_id, &response.header.session_id)) {
        LOG_ERROR("control response session mismatch");
        goto out;
    }
    if (response.header.type != CMD_ACK && response.header.type != CMD_ERROR) {
        LOG_ERROR("unexpected response type %s",
                  cmd_type_to_str(response.header.type));
        goto out;
    }
    if (response.header.payload_len >= sizeof(text)) {
        LOG_ERROR("invalid text response");
        goto out;
    }
    if (response.header.payload_len > 0) {
        memcpy(text, response.payload, response.header.payload_len);
    }
    text[response.header.payload_len] = '\0';

    if (response.header.type == CMD_ERROR ||
        response.header.status != STATUS_OK) {
        if (response.header.status == STATUS_UNAUTHORIZED) {
            result = control_connection_lost(runtime, type);
            goto out;
        }
        fprintf(stderr, "%s failed (status=%d)%s%s\n",
                cmd_type_to_str(type), response.header.status,
                text[0] == '\0' ? "" : ": ", text);
        goto out;
    }
    if (type == CMD_CD) {
        if (text[0] == '\0' || client_runtime_update_cwd(runtime, text) != 0) {
            LOG_ERROR("failed to update remote cwd");
            goto out;
        }
    } else if (text[0] != '\0') {
        printf("%s\n", text);
    }
    result = CLIENT_COMMAND_OK;

out:
    packet_release(&response);
    return result;
}

client_command_result_t client_command_execute(client_runtime_t *runtime,
                                               const char *input)
{
    const char *cursor = input;
    char command[16];
    char first[PATH_MAX];
    cmd_type_t type;
    int needs_argument;

    if (runtime == NULL || input == NULL || next_word(&cursor, command,
                                                      sizeof(command)) != 0) {
        return CLIENT_COMMAND_OK;
    }
    if (strcasecmp(command, "quit") == 0 || strcasecmp(command, "exit") == 0) {
        return CLIENT_COMMAND_EXIT;
    }
    if (strcasecmp(command, "help") == 0 || strcasecmp(command, "h") == 0) {
        menu_show_help();
        return CLIENT_COMMAND_OK;
    }
    if (!client_connection_is_alive(runtime->control_fd)) {
        return control_connection_lost(runtime, CMD_INVALID);
    }
    if (strcasecmp(command, "puts") == 0) {
        if (next_word(&cursor, first, sizeof(first)) != 0) {
            fprintf(stderr, "usage: puts <local>\n");
            return CLIENT_COMMAND_ERROR;
        }
        if (transfer_submit_upload(runtime->transfers, runtime, first) != 0) {
            fprintf(stderr, "unable to start upload (transfer limit reached or invalid path)\n");
            return CLIENT_COMMAND_ERROR;
        }
        printf("upload started in background\n");
        return CLIENT_COMMAND_OK;
    }
    if (strcasecmp(command, "gets") == 0) {
        if (next_word(&cursor, first, sizeof(first)) != 0) {
            fprintf(stderr, "usage: gets <remote>\n");
            return CLIENT_COMMAND_ERROR;
        }
        if (transfer_submit_download(runtime->transfers, runtime, first) != 0) {
            fprintf(stderr, "unable to start download (transfer limit reached, invalid path, or target busy)\n");
            return CLIENT_COMMAND_ERROR;
        }
        printf("download started in background\n");
        return CLIENT_COMMAND_OK;
    }

    if (strcasecmp(command, "pwd") == 0) {
        type = CMD_PWD;
    } else if (strcasecmp(command, "cd") == 0) {
        type = CMD_CD;
    } else if (strcasecmp(command, "ls") == 0) {
        type = CMD_LS;
    } else if (strcasecmp(command, "mkdir") == 0) {
        type = CMD_MKDIR;
    } else if (strcasecmp(command, "rmdir") == 0) {
        type = CMD_RMDIR;
    } else if (strcasecmp(command, "rm") == 0) {
        type = CMD_RM;
    } else {
        type = CMD_INVALID;
    }
    needs_argument = type == CMD_MKDIR || type == CMD_RMDIR || type == CMD_RM;
    if (type != CMD_PWD && type != CMD_CD && type != CMD_LS &&
        !needs_argument) {
        fprintf(stderr, "unsupported command: %s\n", command);
        return CLIENT_COMMAND_ERROR;
    }
    first[0] = '\0';
    if (type != CMD_PWD) {
        (void)next_word(&cursor, first, sizeof(first));
    }
    if (needs_argument && first[0] == '\0') {
        fprintf(stderr, "invalid arguments for %s\n", command);
        return CLIENT_COMMAND_ERROR;
    }
    return execute_short_command(runtime, type, first);
}
