#include "client/runtime.h"

#include <string.h>

#include "client/connection.h"
#include "client/transfer.h"

int client_runtime_init(client_runtime_t *runtime,
                        const client_config_t *config,
                        const char *download_dir)
{
    size_t download_dir_length;

    if (runtime == NULL || config == NULL || download_dir == NULL) {
        return -1;
    }
    download_dir_length = strlen(download_dir);
    if (download_dir_length >= sizeof(runtime->download_dir)) {
        return -1;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->control_fd = -1;
    runtime->config = *config;
    memcpy(runtime->download_dir, download_dir, download_dir_length + 1);

    runtime->transfers = transfer_manager_create(
        (size_t)runtime->config.transfer.max_concurrent);
    if (runtime->transfers == NULL) {
        memset(runtime, 0, sizeof(*runtime));
        runtime->control_fd = -1;
        return -1;
    }

    return 0;
}

void client_runtime_destroy(client_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    if (runtime->transfers != NULL) {
        transfer_manager_stop(runtime->transfers);
        transfer_manager_drain_events(runtime->transfers);
        transfer_manager_destroy(runtime->transfers);
        runtime->transfers = NULL;
    }

    client_runtime_disconnect_control(runtime);
}

void client_runtime_disconnect_control(client_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }
    if (runtime->control_fd >= 0) {
        client_connection_close(runtime->control_fd);
        runtime->control_fd = -1;
    }
    client_runtime_clear_session(runtime);
}

int client_runtime_publish_session(client_runtime_t *runtime,
                                   const session_id_t *session_id,
                                   const char *username)
{
    size_t username_length;

    if (runtime == NULL || session_id_is_empty(session_id) || username == NULL) {
        return -1;
    }
    username_length = strnlen(username, sizeof(runtime->username));
    if (username_length == 0 || username_length == sizeof(runtime->username)) {
        return -1;
    }
    runtime->session_id = *session_id;
    memcpy(runtime->username, username, username_length + 1);
    runtime->remote_cwd[0] = '/';
    runtime->remote_cwd[1] = '\0';
    return 0;
}

void client_runtime_clear_session(client_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    memset(&runtime->session_id, 0, sizeof(runtime->session_id));
    memset(runtime->username, 0, sizeof(runtime->username));
    memset(runtime->remote_cwd, 0, sizeof(runtime->remote_cwd));
}

int client_runtime_session_snapshot(client_runtime_t *runtime,
                                    session_id_t *session_id,
                                    char *username,
                                    size_t username_size,
                                    char *cwd,
                                    size_t cwd_size)
{
    size_t username_length;
    size_t cwd_length;

    if (runtime == NULL || session_id == NULL || username == NULL ||
        username_size == 0 || cwd == NULL || cwd_size == 0) {
        return -1;
    }
    username_length = strnlen(runtime->username, sizeof(runtime->username));
    cwd_length = strnlen(runtime->remote_cwd, sizeof(runtime->remote_cwd));
    if (session_id_is_empty(&runtime->session_id) ||
        username_length == sizeof(runtime->username) ||
        cwd_length == sizeof(runtime->remote_cwd) ||
        username_length >= username_size || cwd_length >= cwd_size) {
        return -1;
    }

    *session_id = runtime->session_id;
    memcpy(username, runtime->username, username_length + 1);
    memcpy(cwd, runtime->remote_cwd, cwd_length + 1);

    return 0;
}

int client_runtime_update_cwd(client_runtime_t *runtime, const char *cwd)
{
    size_t cwd_length;

    if (runtime == NULL || cwd == NULL) {
        return -1;
    }
    cwd_length = strnlen(cwd, sizeof(runtime->remote_cwd));
    if (cwd_length == 0 || cwd_length == sizeof(runtime->remote_cwd)) {
        return -1;
    }
    if (session_id_is_empty(&runtime->session_id)) {
        return -1;
    }

    memcpy(runtime->remote_cwd, cwd, cwd_length + 1);
    return 0;
}
