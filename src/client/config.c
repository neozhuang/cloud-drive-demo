#include "client/config.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "common/utils.h"

int client_config_load(client_config_t *config, const char *filename)
{
    FILE* fp;
    char* key;
    char* value;
    char* eq;
    char line[4096];

    if ((fp = fopen(filename, "r")) == NULL) {
        printf("failed to open file %s\n", filename);
        return -1;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        // line: server_ip = 127.0.0.1
        line[strcspn(line, "\n")] = '\0';
        eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            key = trim(line);
            value = trim(eq + 1);
        } else { 
            // value is null jump this line
            continue;
        }
        if (strcasecmp(key, "server_ip") == 0) {
            strcpy(config->server_ip, value);
        } else if (strcasecmp(key, "server_port") == 0) {
            strcpy(config->server_port, value);
        }
    }

    return 0;
}

void client_config_print(const client_config_t *config)
{
    printf("server_ip = %s\n", config->server_ip);
    printf("server_port = %s\n", config->server_port);
}
