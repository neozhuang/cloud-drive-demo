#include "server/config.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "inih/ini.h"

static int handler(void* user, const char* section, const char* name,
                   const char* value)
{
    server_config_t* pconfig = (server_config_t*)user;

#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("network", "host")) {
        strncpy(pconfig->network.host, value, sizeof(pconfig->network.host) - 1);
        pconfig->network.host[sizeof(pconfig->network.host) - 1] = '\0';
    } else if (MATCH("network", "port")) {
        strncpy(pconfig->network.port, value, sizeof(pconfig->network.port) - 1);
        pconfig->network.port[sizeof(pconfig->network.port) - 1] = '\0';
    } else if (MATCH("network", "backlog")) {
        pconfig->network.backlog = atoi(value);
    } else if (MATCH("mysql", "host")) {
        strncpy(pconfig->mysql.host, value, sizeof(pconfig->mysql.host) - 1);
        pconfig->mysql.host[sizeof(pconfig->mysql.host) - 1] = '\0';
    } else if (MATCH("mysql", "port")) {
        strncpy(pconfig->mysql.port, value, sizeof(pconfig->mysql.port) - 1);
        pconfig->mysql.port[sizeof(pconfig->mysql.port) - 1] = '\0';
    } else if (MATCH("mysql", "user")) {
        strncpy(pconfig->mysql.user, value, sizeof(pconfig->mysql.user) - 1);
        pconfig->mysql.user[sizeof(pconfig->mysql.user) - 1] = '\0';
    } else if (MATCH("mysql", "password")) {
        strncpy(pconfig->mysql.password, value, sizeof(pconfig->mysql.password) - 1);
        pconfig->mysql.password[sizeof(pconfig->mysql.password) - 1] = '\0';
    } else if (MATCH("mysql", "database")) {
        strncpy(pconfig->mysql.database, value, sizeof(pconfig->mysql.database) - 1);
        pconfig->mysql.database[sizeof(pconfig->mysql.database) - 1] = '\0';
    } else if (MATCH("mysql", "charset")) {
        strncpy(pconfig->mysql.charset, value, sizeof(pconfig->mysql.charset) - 1);
        pconfig->mysql.charset[sizeof(pconfig->mysql.charset) - 1] = '\0';
    } else if (MATCH("mysql", "pool_size")) {
        pconfig->mysql.pool_size = atoi(value);
    } else if (MATCH("storage", "root_dir")) {
        strncpy(pconfig->storage.root_dir, value, sizeof(pconfig->storage.root_dir) - 1);
        pconfig->storage.root_dir[sizeof(pconfig->storage.root_dir) - 1] = '\0';
    } else if (MATCH("storage", "transfer_temp_dir")) {
        strncpy(pconfig->storage.transfer_temp_dir, value, sizeof(pconfig->storage.transfer_temp_dir) - 1);
        pconfig->storage.transfer_temp_dir[sizeof(pconfig->storage.transfer_temp_dir) - 1] = '\0';
    } else if (MATCH("thread_pool", "thread_num")) {
        pconfig->thread_pool.thread_num = atoi(value);
    } else if (MATCH("thread_pool", "queue_capacity")) {
        pconfig->thread_pool.queue_capacity = atoi(value);
    } else if (MATCH("log", "log_level")) {
        strncpy(pconfig->log.log_level, value, sizeof(pconfig->log.log_level) - 1);
        pconfig->log.log_level[sizeof(pconfig->log.log_level) - 1] = '\0';
    } else if (MATCH("log", "log_file")) {
        strncpy(pconfig->log.log_file, value, sizeof(pconfig->log.log_file) - 1);
        pconfig->log.log_file[sizeof(pconfig->log.log_file) - 1] = '\0';
    } else {
        return 0;  /* unknown section/name, error */
    }
    return 1;
}

int server_config_load(server_config_t *pconfig, const char* filename)
{
    if (ini_parse(filename, handler, pconfig) < 0) {
        printf("Cannot load %s\n", filename);
        return -1;
    }
    return 0;
}

void server_config_print(const server_config_t *pconfig)
{
    printf("[network]\n");
    printf("host = %s\n", pconfig->network.host);
    printf("port = %s\n", pconfig->network.port);
    printf("backlog = %d\n", pconfig->network.backlog);

    printf("\n[mysql]\n");
    printf("host = %s\n", pconfig->mysql.host);
    printf("port = %s\n", pconfig->mysql.port);
    printf("user = %s\n", pconfig->mysql.user);
    printf("password = %s\n", pconfig->mysql.password);
    printf("database = %s\n", pconfig->mysql.database);
    printf("charset = %s\n", pconfig->mysql.charset);
    printf("pool_size = %d\n", pconfig->mysql.pool_size);

    printf("\n[storage]\n");
    printf("root_dir = %s\n", pconfig->storage.root_dir);
    printf("transfer_temp_dir = %s\n", pconfig->storage.transfer_temp_dir);

    printf("\n[thread_pool]\n");
    printf("thread_num = %d\n", pconfig->thread_pool.thread_num);
    printf("queue_capacity = %d\n", pconfig->thread_pool.queue_capacity);

    printf("\n[log]\n");
    printf("log_level = %s\n", pconfig->log.log_level);
    printf("log_file = %s\n", pconfig->log.log_file);
}
