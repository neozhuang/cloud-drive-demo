#pragma once

typedef enum client_status_e {
    CLIENT_STATE_INIT = 0,
    CLIENT_STATE_DISCONNECTED,
    CLIENT_STATE_CONNECTING,
    CLIENT_STATE_CONNECTED,
    CLIENT_STATE_LOGGED_IN,
    CLIENT_STATE_TRANSFERRING,
    CLIENT_STATE_ERROR
} client_status_t;

typedef struct client_state_s {
    client_status_t status;
    int sock_fd;
    char username[64];
    char remote_cwd[256];
} client_state_t;

