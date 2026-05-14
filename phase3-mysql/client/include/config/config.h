#ifndef __CLIENT_CONFIG_H__
#define __CLIENT_CONFIG_H__

#define CLIENT_CONFIG_DEFAULT_PATH "config/client.ini"

#define CLIENT_CONFIG_HOST_LEN 64
#define CLIENT_CONFIG_PATH_LEN 256
#define CLIENT_CONFIG_LOG_LEVEL_LEN 16

typedef struct ClientServerConfig {
    char host[CLIENT_CONFIG_HOST_LEN];
    int port;
} ClientServerConfig;

typedef struct ClientLogConfig {
    char level[CLIENT_CONFIG_LOG_LEVEL_LEN];
    char file[CLIENT_CONFIG_PATH_LEN];
} ClientLogConfig;

typedef struct ClientConfig {
    ClientServerConfig server;
    ClientLogConfig log;
} ClientConfig;

void client_config_init_defaults(ClientConfig *config);
int client_config_load(ClientConfig *config, const char *path);
void client_config_print(const ClientConfig *config);

#endif
