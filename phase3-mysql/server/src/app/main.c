#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <unistd.h>

#include "config/config.h"
#include "app/process_manager.h"
#include "ui/term_ui.h"

int main(int argc, char *argv[])
{
    const char *config_path = argc > 1 ? argv[1] : NULL;
    ServerConfig config;
    int shutdown_pipe[2];
    pid_t child_pid;

    // draw banner
    term_ui_print_banner();
    term_ui_print_start_time();

    // load config
    if (config_load(&config, config_path) != 0) {
        fprintf(stderr, "Failed to load server config: %s\n",
                config_path == NULL ? CONFIG_DEFAULT_PATH : config_path);
        return 1;
    }

    // config_print(&config);

    fflush(NULL);

    if (pipe(shutdown_pipe) != 0) {
        perror("Failed to create shutdown pipe");
        return 1;
    }

    child_pid = fork();
    if (child_pid < 0) {
        perror("Failed to fork child process");
        close(shutdown_pipe[0]);
        close(shutdown_pipe[1]);
        return 1;
    }

    // parent
    if (child_pid > 0) {
        close(shutdown_pipe[0]);
        return server_run_parent_process(child_pid, shutdown_pipe[1]);
    }

    // child
    close(shutdown_pipe[1]);
    return server_run_network_process(&config, shutdown_pipe[0]);
}
