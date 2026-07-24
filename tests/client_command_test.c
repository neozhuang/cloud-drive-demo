#include "client/command.h"
#include "client/runtime.h"
#include "common/log.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    client_config_t config;
    client_runtime_t runtime;
    session_id_t session_id = {{1}};
    session_id_t snapshot_id;
    char username[64];
    char cwd[PATH_MAX];
    int sockets[2];

    memset(&config, 0, sizeof(config));
    config.transfer.max_concurrent = 1;
    assert(log_init("error", NULL) == 0);
    assert(client_runtime_init(&runtime, &config, "/tmp") == 0);
    assert(client_runtime_publish_session(&runtime, &session_id, "alice") == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    runtime.control_fd = sockets[0];
    close(sockets[1]);

    assert(client_command_execute(&runtime, "pwd") ==
           CLIENT_COMMAND_RECONNECT);
    assert(runtime.control_fd == -1);
    assert(client_runtime_session_snapshot(&runtime, &snapshot_id,
                                            username, sizeof(username),
                                            cwd, sizeof(cwd)) == -1);
    assert(client_command_execute(&runtime, "quit") == CLIENT_COMMAND_EXIT);

    client_runtime_destroy(&runtime);
    log_close();
    puts("client command tests passed");
    return 0;
}
