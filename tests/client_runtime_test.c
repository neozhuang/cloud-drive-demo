#include "client/runtime.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    client_config_t config;
    client_runtime_t runtime;
    session_id_t session_id;
    session_id_t snapshot_id;
    char username[64];
    char cwd[PATH_MAX];
    int sockets[2];

    memset(&config, 0, sizeof(config));
    strcpy(config.remote.host, "127.0.0.1");
    strcpy(config.remote.port, "8888");
    strcpy(config.log.log_level, "info");
    strcpy(config.log.log_file, "logs/client.log");
    strcpy(config.storage.download_dir, "downloads");
    config.transfer.max_concurrent = 2;
    config.transfer.connect_timeout_ms = 1000;
    config.transfer.io_timeout_ms = 1000;

    memset(&session_id, 0, sizeof(session_id));
    session_id.bytes[0] = 1U;

    assert(client_runtime_init(&runtime, &config,
                               "/tmp/project/downloads") == 0);
    assert(client_runtime_session_snapshot(&runtime, &snapshot_id,
                                           username, sizeof(username),
                                           cwd, sizeof(cwd)) == -1);

    assert(client_runtime_publish_session(&runtime, &session_id, "alice") == 0);
    assert(client_runtime_session_snapshot(&runtime, &snapshot_id,
                                           username, sizeof(username),
                                           cwd, sizeof(cwd)) == 0);
    assert(session_id_equal(&session_id, &snapshot_id));
    assert(strcmp(username, "alice") == 0);
    assert(strcmp(cwd, "/") == 0);

    assert(client_runtime_update_cwd(&runtime, "/docs") == 0);
    assert(client_runtime_session_snapshot(&runtime, &snapshot_id,
                                           username, sizeof(username),
                                           cwd, sizeof(cwd)) == 0);
    assert(strcmp(cwd, "/docs") == 0);

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    runtime.control_fd = sockets[0];
    client_runtime_disconnect_control(&runtime);
    assert(runtime.control_fd == -1);
    assert(read(sockets[1], username, 1) == 0);
    close(sockets[1]);
    assert(client_runtime_session_snapshot(&runtime, &snapshot_id,
                                           username, sizeof(username),
                                           cwd, sizeof(cwd)) == -1);
    client_runtime_destroy(&runtime);

    puts("client runtime tests passed");
    return 0;
}
