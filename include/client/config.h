#pragma once

typedef struct client_config_s{
    char server_ip[16];
    char server_port[16];
} client_config_t;

int client_config_load(client_config_t *config, const char *path);
void client_config_print(const client_config_t *config);

