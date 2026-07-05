#include <linux/limits.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "client/config.h"
#include "client/state.h"
#include "client/network.h"
#include "client/user_auth.h"
#include "common/protocol.h"
#include "common/utils.h"
#include "client/handler.h"
#include "common/tui.h"
#include "common/log.h"
#include "client/menu.h"


int main(int argc, char *argv[]) {
    int sock_fd;
    client_config_t config;         // client config
    client_state_t client_state;
    char project_dir[PATH_MAX];     // cloud-drive-demo
    char config_path[PATH_MAX];     // config file path
    char log_file[PATH_MAX];        // log file absolute path 
    char download_dir[PATH_MAX];    // client download directory
    char input[PATH_MAX];           // user input request

    /* Print a beautiful banner and the current time */
    tui_print_banner();
    tui_print_time("Cloud Drive Demo Client");

    /* Accept an optional config path; otherwise fall back to config/client.conf
     * under the detected project directory.
     */
    if (argc > 2) {
        fprintf(stderr, "usage: %s [config-file]\n", argv[0]);
        return 1;
    }
    if (argc == 2) {
        strcpy(config_path, argv[1]);
    } else {
        if (get_project_dir(project_dir, sizeof(project_dir)) != 0)
            return -1; 
        if(join_path(config_path, sizeof(config_path), project_dir, "config/client.conf") != 0)
            return -1;
    }

    /* Load and print the client configuration before opening the connection. */
    if(client_config_load(&config, config_path) != 0)
        return -1;
    // client_config_print(&config);

    // Initialize logging before network startup so later failures are recorded.
    if (join_path(log_file, sizeof(log_file), project_dir, config.log.log_file) != 0)
        return -1;
    if (ensure_parent_dir(log_file) != 0) {
        return -1;
    }
    if (log_init(config.log.log_level, log_file) != 0) {
        fprintf(stderr, "log_init failed, fallback to stdout\n");
    }

    // Initialize download directory.
    if (join_path(download_dir, sizeof(download_dir), project_dir, config.storage.download_dir) != 0)
        return -1;
    if (ensure_parent_dir(download_dir) != 0) {
        return -1;
    }
    memset(&client_state, 0, sizeof(client_state));
    strncpy(client_state.download_dir, download_dir, sizeof(download_dir) - 1);
    client_state.download_dir[sizeof(download_dir) - 1] = '\0';

    /* Connect to the configured server and initialize connection state. */
    sock_fd = network_connect(&client_state, config.remote.host, config.remote.port);
    if (sock_fd < 0) {
        LOG_ERROR("Failed to connect server");
        return -1;
    }
    LOG_INFO("Connected to remote server");

    while (1) {
        int auth_result = user_auth(&client_state);

        if (auth_result == AUTH_EXIT) {
            printf("bye, have a good day!\n");
            break;
        }
        if (auth_result != AUTH_OK) {
            continue;
        }

        // Here auth_result == AUTH_OK 
        LOG_INFO("User %s login success", client_state.username);

        // clear screen and show available commands
        tui_clear_screen();
        menu_show_help();

        /* Interactive command loop: show prompt, read input, and dispatch commands. */
        while (1) {
            printf("%s@cloud-drive:%s> ", client_state.username, client_state.remote_cwd);
            fflush(stdout);

            // EOF or input error exits the loop cleanly.
            if (s_gets(input, sizeof(input)) == NULL) {
                break;
                // continue to menu choose
            }

            // Let the user exit with common shell-like commands.
            if (strcasecmp(input, "quit") == 0 || strcasecmp(input, "exit") == 0) {
                printf("bye, have a good day!\n");
                LOG_INFO("client exit, current user: %s", client_state.username);
                goto cleanup;
            }

            // show help menu
            if (strcasecmp(input, "help") == 0 || strcasecmp(input, "h") == 0) {
                menu_show_help();
                continue;
            }

            // user enter the empty line, just continue
            if (strlen(input) == 0) {
                continue;
            }

            handle_command_input(&client_state, input);
        }
    }

cleanup:
    close(sock_fd);
    log_close();
    return 0;
}
