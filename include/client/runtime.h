#pragma once

#include <stddef.h>

#include "client/config.h"
#include "common/protocol.h"

typedef struct transfer_manager transfer_manager_t;

typedef struct client_runtime {
    client_config_t config;
    int control_fd;

    /* Session state belongs to the main thread; workers use task snapshots. */
    session_id_t session_id;
    char username[64];
    char remote_cwd[PATH_MAX];

    char download_dir[PATH_MAX];

    transfer_manager_t *transfers;
} client_runtime_t;

int client_runtime_init(client_runtime_t *runtime,
                        const client_config_t *config,
                        const char *download_dir);
void client_runtime_destroy(client_runtime_t *runtime);
void client_runtime_disconnect_control(client_runtime_t *runtime);

int client_runtime_publish_session(client_runtime_t *runtime,
                                   const session_id_t *session_id,
                                   const char *username);
void client_runtime_clear_session(client_runtime_t *runtime);
int client_runtime_session_snapshot(client_runtime_t *runtime,
                                    session_id_t *session_id,
                                    char *username,
                                    size_t username_size,
                                    char *cwd,
                                    size_t cwd_size);
int client_runtime_update_cwd(client_runtime_t *runtime, const char *cwd);
