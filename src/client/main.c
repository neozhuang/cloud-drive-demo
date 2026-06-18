#include <linux/limits.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "client/config.h"
#include "client/state.h"
#include "client/network.h"
#include "client/auth.h"
#include "common/protocol.h"
#include "common/utils.h"
#include "client/handler.h"


int main(int argc, char *argv[]) {
    int sock_fd;
    client_config_t config;
    client_state_t client_state;
    char project_dir[PATH_MAX];
    char config_path[PATH_MAX];
    const char* path = NULL;
    char input[PATH_MAX];

    /* Accept an optional config path; otherwise fall back to config/client.conf
     * under the detected project directory.
     */
    if (argc > 2) {
        fprintf(stderr, "usage: %s [config-file]\n", argv[0]);
        return 1;
    }
    if (argc == 2) {
        path = argv[1];
    } else {
        if (get_project_dir(project_dir, sizeof(project_dir)) != 0)
            return -1; 
        if(join_path(config_path, sizeof(config_path), project_dir, "config/client.conf") != 0)
            return -1;
        path = config_path;
    }

    /* Load and print the client configuration before opening the connection. */
    if(client_config_load(&config, path) != 0)
        return -1;
    client_config_print(&config);

    /* Connect to the configured server and initialize connection state. */
    sock_fd = network_connect(&client_state, config.server_ip, config.server_port);
    if (sock_fd < 0) {
        printf("failed to connect server\n");
        return -1;
    }

    /* Authenticate the user before accepting interactive commands. */
    if (user_login(&client_state) != 0){
        return -1;
    }
    // printf("username %s login success\n", client_state.username);

    /* Interactive command loop: show prompt, read input, and dispatch commands. */
    while (1) {
        // Print prompt with the current user and remote working directory.
        printf("%s@cloud-drive:%s> ", client_state.username, client_state.remote_cwd);
        fflush(stdout);

        // EOF or input error exits the loop cleanly.
        if (s_gets(input, sizeof(input)) == NULL) {
            break;
        }

        // Let the user exit with common shell-like commands.
        if (strcasecmp(input, "quit") == 0 || strcasecmp(input, "exit") == 0) {
            printf("bye, have a good day!\n");
            break;
        }

        // Ignore empty lines instead of passing them to the command handler.
        if (strlen(input) == 0) {
            continue;
        }
        handle_command_input(&client_state, input);
    }

    /* Close the server connection before terminating the client. */
    close(sock_fd);
    return 0;
}
