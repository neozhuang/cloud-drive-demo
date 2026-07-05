#pragma once

#include "common/protocol.h"
#include "server/database_pool.h"

typedef struct {
    int client_fd;
    packet_t packet;
    database_pool_t* db_pool;
    char storage_root[PATH_MAX];    // absolute path: project_dir + config.storage.root_dir;
} packet_task_t;

void handle_basic_task(void *arg);

