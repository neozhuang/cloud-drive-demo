#include "server/config.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "common/utils.h"

int server_config_load(server_config_t *config, const char* filename)
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
        if (strcasecmp(key, "host") == 0) {
            strcpy(config->host, value);
        } else if (strcasecmp(key, "port") == 0) {
            strcpy(config->port, value);
        } else if (strcasecmp(key, "backlog") == 0) {
            config->backlog = atoi(value);
        } else if (strcasecmp(key, "root_dir") == 0) {
            strcpy(config->root_dir, value);
        } else if (strcasecmp(key, "thread_num") == 0) {
            config->thread_num = atoi(value);
        } else if (strcasecmp(key, "queue_capacity") == 0) {
            config->queue_capacity = atoi(value);
        } else if (strcasecmp(key, "log_level") == 0) {
            strcpy(config->log_level, value);
        } else if (strcasecmp(key, "log_file") == 0) {
            strcpy(config->log_file, value);
        }
    }

    return 0;
}

void server_config_print(const server_config_t *config)
{
    printf("host = %s\n", config->host);
    printf("port = %s\n", config->port);
    printf("backlog = %d\n", config->backlog);
    printf("root_dir = %s\n", config->root_dir);
    printf("thread_num = %d\n", config->thread_num);
    printf("queue_capacity = %d\n", config->queue_capacity);
    printf("log_level = %s\n", config->log_level);
    printf("log_file = %s\n", config->log_file);
}
