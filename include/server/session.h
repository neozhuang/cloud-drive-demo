#pragma once

#include <stddef.h>
#include <stdint.h>

#include "common/protocol.h"

typedef struct session_table session_table_t;

typedef struct {
    uint64_t user_id;
    uint64_t cwd_id;
    char username[64];
    char cwd[PATH_MAX];
} session_context_t;

session_table_t *session_table_create(void);
void session_table_destroy(session_table_t *table);

int session_create(session_table_t *table,
                   int client_fd,
                   uint64_t user_id,
                   const char *username,
                   uint64_t root_path_id,
                   session_id_t *out_session_id);

int session_authorize(session_table_t *table,
                      int client_fd,
                      const session_id_t *session_id,
                      session_context_t *context);

int session_update_cwd(session_table_t *table,
                       int client_fd,
                       const session_id_t *session_id,
                       uint64_t cwd_id,
                       const char *cwd);

void session_detach_fd(session_table_t *table, int client_fd);
