#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "client/command.h"
#include "client/config.h"
#include "client/connection.h"
#include "client/menu.h"
#include "client/runtime.h"
#include "client/transfer.h"
#include "client/user_auth.h"
#include "common/log.h"
#include "common/protocol.h"
#include "common/tui.h"
#include "common/utils.h"

static int load_paths_and_config(int argc, char **argv, client_config_t *config,
                                 char *project_dir, char *download_dir,
                                 char *log_file)
{
    char config_path[PATH_MAX];
    int written;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [config-file]\n", argv[0]);
        return -1;
    }
    if (get_project_dir(project_dir, PATH_MAX) != 0) {
        fprintf(stderr, "failed to locate project directory\n");
        return -1;
    }
    if (argc == 2) {
        written = snprintf(config_path, sizeof(config_path), "%s", argv[1]);
        if (written < 0 || (size_t)written >= sizeof(config_path)) {
            fprintf(stderr, "config path is too long\n");
            return -1;
        }
    } else if (join_path(config_path, sizeof(config_path), project_dir,
                         "config/client.conf") != 0) {
        return -1;
    }

    if (client_config_load(config, config_path) != 0) {
        fprintf(stderr, "invalid client configuration: %s\n", config_path);
        return -1;
    }
    if (join_path(log_file, PATH_MAX, project_dir, config->log.log_file) != 0 ||
        join_path(download_dir, PATH_MAX, project_dir,
                  config->storage.download_dir) != 0) {
        fprintf(stderr, "configured path is too long\n");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    client_config_t config;
    client_runtime_t runtime;
    client_command_result_t command_result;
    char project_dir[PATH_MAX];
    char download_dir[PATH_MAX];
    char log_file[PATH_MAX];
    char input[MAX_COMMAND_INPUT];
    char username[64];
    char cwd[PATH_MAX];
    session_id_t session_id;
    int runtime_initialized = 0;
    int logging_initialized = 0;
    int exit_code = 1;

    memset(&runtime, 0, sizeof(runtime));
    runtime.control_fd = -1;
    tui_print_banner();
    tui_print_time("Cloud Drive Demo Client");

    if (load_paths_and_config(argc, argv, &config, project_dir, download_dir,
                              log_file) != 0) {
        goto cleanup;
    }
    if (ensure_parent_dir(log_file) != 0) {
        perror("create log directory");
        goto cleanup;
    }
    if (log_init(config.log.log_level, log_file) != 0) {
        fprintf(stderr, "log_init failed, fallback to stdout\n");
    }
    logging_initialized = 1;
    if (mkdir_p(download_dir, 0755) != 0) {
        perror("create download directory");
        goto cleanup;
    }
    if (client_runtime_init(&runtime, &config, download_dir) != 0) {
        LOG_ERROR("Failed to initialize client runtime");
        goto cleanup;
    }
    runtime_initialized = 1;

    signal(SIGPIPE, SIG_IGN);

    runtime.control_fd = client_connection_open(
        config.remote.host, config.remote.port,
        config.transfer.connect_timeout_ms, config.transfer.io_timeout_ms);
    if (runtime.control_fd < 0) {
        LOG_ERROR("Failed to connect server");
        goto cleanup;
    }
    LOG_INFO("Connected to remote server");

    if (user_auth(&runtime) != AUTH_OK) {
        // Only AUTH_EXIT
        printf("bye, have a good day!\n");
        exit_code = 0;  // Normal exit
        goto cleanup;
    }

    tui_clear_screen();
    menu_show_help();

    while (1) {
        transfer_manager_drain_events(runtime.transfers);
        if (client_runtime_session_snapshot(&runtime, &session_id,
                                            username, sizeof(username),
                                            cwd, sizeof(cwd)) != 0) {
            LOG_ERROR("Session is no longer available");
            break;
        }
        printf("%s@cloud-drive:%s> ", username, cwd);
        fflush(stdout);
        if (s_gets(input, sizeof(input)) == NULL) {
            break;
        }
        command_result = client_command_execute(&runtime, input);
        if (command_result == CLIENT_COMMAND_EXIT) {
            break;
        }
    }

    printf("bye, have a good day!\n");
    exit_code = 0;

cleanup:
    if (runtime_initialized) {
        client_runtime_destroy(&runtime);
    }
    if (logging_initialized) {
        log_close();
    }
    return exit_code;
}
