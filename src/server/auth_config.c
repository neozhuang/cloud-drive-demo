#include "server/auth_config.h"

#include <stdio.h>
#include <string.h>

#include "common/utils.h"

int auth_config_load(auth_config_t *config, const char *path)
{
    FILE* fp;
    char line[4096];
    char* username;
    memset(config, 0, sizeof(*config));

    if ((fp = fopen(path, "r")) == NULL) {
        printf("failed to open file %s\n", path);
        return -1;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        username = trim(line);
        if (username[0] == '\0') {
            continue; 
        }
        strncpy(config->users[config->user_count].username, username, strlen(username));
        config->users[config->user_count++].username[MAX_USERNAME_LEN - 1] = '\0';
    }
    return 0;
}

int auth_check_user(const auth_config_t *config, const char *username)
{
    if (config == NULL || config->user_count == 0 || username == NULL) {
        return 0;
    }
    for (int i = 0; i < config->user_count; i++) {
        if (strcmp(config->users[i].username, username) == 0) {
            return 1; 
        }
    }
    return 0;
}
