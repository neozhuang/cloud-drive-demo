#ifndef CLOUD_DRIVE_APP_PROCESS_MANAGER_H
#define CLOUD_DRIVE_APP_PROCESS_MANAGER_H

#include <sys/types.h>

#include "config/config.h"

int server_run_parent_process(pid_t child_pid, int shutdown_write_fd);
int server_run_network_process(const ServerConfig *config, int shutdown_read_fd);

#endif
