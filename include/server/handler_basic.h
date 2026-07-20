#pragma once

#include "common/protocol.h"
#include "server/database_pool.h"
#include "server/session.h"

typedef struct {
    int client_fd;
    packet_t packet;
    database_pool_t* db_pool;
    session_table_t *session_table;
    session_context_t session;
    char storage_root[PATH_MAX];    // absolute path: project_dir + config.storage.root_dir;
} packet_task_t;

void handle_basic_task(void *arg);
