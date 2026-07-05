#include "client/config.h"

#include <stdio.h>
#include <string.h>

#include "inih/ini.h"

static int handler(void* user, const char* section, const char* name,
                   const char* value)
{
    client_config_t* pconfig = (client_config_t*)user;

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("remote", "host")) {
        strncpy(pconfig->remote.host, value, strlen(value));
        pconfig->remote.host[strlen(value)] = '\0';
    } else if (MATCH("remote", "port")) {
        strncpy(pconfig->remote.port, value, strlen(value));
        pconfig->remote.port[strlen(value)] = '\0';
    } else if (MATCH("log", "log_level")) {
        strncpy(pconfig->log.log_level, value, strlen(value));
        pconfig->log.log_level[strlen(value)] = '\0';
    } else if (MATCH("log", "log_file")) {
        strncpy(pconfig->log.log_file, value, strlen(value));
        pconfig->log.log_file[strlen(value)] = '\0';
    } else if (MATCH("storage", "download_dir")) {
        strncpy(pconfig->storage.download_dir, value, strlen(value));
        pconfig->storage.download_dir[strlen(value)] = '\0';
    } else {
        return 0;  /* unknown section/name, error */
    }
    return 1;
}

int client_config_load(client_config_t *config, const char *filename)
{
    if (ini_parse(filename, handler, config) < 0) {
        printf("Cannot load %s\n", filename);
        return -1;
    }
    return 0;
}

void client_config_print(const client_config_t *config)
{
    printf("[remote]\n");
    printf("host = %s\n", config->remote.host);
    printf("port = %s\n", config->remote.port);

    printf("\n[log]\n");
    printf("log_level = %s\n", config->log.log_level);
    printf("log_file = %s\n", config->log.log_file);

    printf("\n[storage]\n");
    printf("download_dir = %s\n", config->storage.download_dir);

    printf("\n");
}
