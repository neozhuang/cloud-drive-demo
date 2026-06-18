#pragma once

#include <limits.h>
#include <linux/limits.h>

typedef struct server_config_s {
    char host[16];              // server bind address
    char port[16];              // server port
    int backlog;                // accept queue length
    char root_dir[PATH_MAX];    // cloud drive root
    int thread_num;             // thread number
    int queue_capacity;         // task queue capacity
    char log_level[16];         // log level
    char log_file[PATH_MAX];    // log file
} server_config_t;

int server_config_load(server_config_t *config, const char* filename);
void server_config_print(const server_config_t *config);

