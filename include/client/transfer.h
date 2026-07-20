#pragma once

#include <stddef.h>

typedef struct client_runtime client_runtime_t;
typedef struct transfer_manager transfer_manager_t;

transfer_manager_t *transfer_manager_create(size_t max_concurrent);
void transfer_manager_stop(transfer_manager_t *manager);
void transfer_manager_destroy(transfer_manager_t *manager);
void transfer_manager_drain_events(transfer_manager_t *manager);

int transfer_submit_upload(transfer_manager_t *manager,
                           client_runtime_t *runtime,
                           const char *local_path);
int transfer_submit_download(transfer_manager_t *manager,
                             client_runtime_t *runtime,
                             const char *remote_path);
