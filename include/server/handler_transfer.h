#pragma once

#include "common/protocol.h"
#include "server/database_pool.h"
#include <linux/limits.h>

typedef struct {
    int epoll_fd;
    int client_fd;
    packet_t packet;
    database_pool_t* db_pool;
    char storage_root[PATH_MAX];    // absolute path: project_dir + config.storage.root_dir;
    char transfer_temp_dir[PATH_MAX];
} transfer_task_t;

void handle_transfer_task(void* arg);
