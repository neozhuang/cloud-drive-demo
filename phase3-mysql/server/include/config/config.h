#ifndef CLOUD_DRIVE_CONFIG_H
#define CLOUD_DRIVE_CONFIG_H

#include <stddef.h>

#define CONFIG_DEFAULT_PATH "config/server.ini"

#define CONFIG_HOST_LEN 64
#define CONFIG_USER_LEN 64
#define CONFIG_PASSWORD_LEN 128
#define CONFIG_DB_LEN 64
#define CONFIG_CHARSET_LEN 32
#define CONFIG_PATH_LEN 256
#define CONFIG_LOG_LEVEL_LEN 16

typedef struct ListenConfig {
    char host[CONFIG_HOST_LEN];
    int port;
    int backlog;
} ListenConfig;

typedef struct MysqlConfig {
    char host[CONFIG_HOST_LEN];
    int port;
    char user[CONFIG_USER_LEN];
    char password[CONFIG_PASSWORD_LEN];
    char database[CONFIG_DB_LEN];
    char charset[CONFIG_CHARSET_LEN];
} MysqlConfig;

typedef struct StorageConfig {
    char root_dir[CONFIG_PATH_LEN];
} StorageConfig;

typedef struct ThreadPoolConfig {
    int worker_threads;
    int queue_capacity;
} ThreadPoolConfig;

typedef struct LogConfig {
    char level[CONFIG_LOG_LEVEL_LEN];
    char file[CONFIG_PATH_LEN];
} LogConfig;

typedef struct ServerConfig {
    ListenConfig listen;
    MysqlConfig mysql;
    StorageConfig storage;
    ThreadPoolConfig thread_pool;
    LogConfig log;
} ServerConfig;

void config_init_defaults(ServerConfig *config);
int config_load(ServerConfig *config, const char *path);
void config_print(const ServerConfig *config);

#endif
