#pragma once

#include <limits.h>

typedef struct network_config_s {
    char host[64];
    char port[16];
    int backlog;
} network_config_t;

typedef struct mysql_config_s {
    char host[64];
    char port[16];
    char user[64];
    char password[128];
    char database[64];
    char charset[32];
    int pool_size;
} mysql_config_t;

typedef struct storage_config_s {
    char root_dir[PATH_MAX];
    char transfer_temp_dir[PATH_MAX];
} storage_config_t;

typedef struct thread_pool_config_s {
    int thread_num;
    int queue_capacity;
} thread_pool_config_t;

typedef struct log_config_s {
    char log_level[16];
    char log_file[PATH_MAX];
} log_config_t;

typedef struct session_config_s {
    int idle_timeout_seconds;
} session_config_t;

typedef struct server_config_s {
    network_config_t network;
    mysql_config_t mysql;
    storage_config_t storage;
    thread_pool_config_t thread_pool;
    session_config_t session;
    log_config_t log;
} server_config_t;

int server_config_load(server_config_t *config, const char* filename);
void server_config_print(const server_config_t *config);
