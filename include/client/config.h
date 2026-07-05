#pragma once

#include <limits.h>

typedef struct remote_config_s {
    char host[64];
    char port[16];
} remote_config_t;

typedef struct log_config_s {
    char log_level[16];
    char log_file[PATH_MAX];
} log_config_t;

typedef struct storage_config_s {
    char download_dir[PATH_MAX];
} storage_config_t;

typedef struct client_config_s {
    remote_config_t remote;
    log_config_t log;
    storage_config_t storage;
} client_config_t;

int client_config_load(client_config_t *config, const char *path);
void client_config_print(const client_config_t *config);
