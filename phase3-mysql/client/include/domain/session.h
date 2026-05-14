#ifndef CLIENT_DOMAIN_SESSION_H
#define CLIENT_DOMAIN_SESSION_H

#define CLIENT_USERNAME_LEN 64
#define CLIENT_PATH_LEN 256

typedef enum ClientState {
    CLIENT_STATE_INIT = 0,
    CLIENT_STATE_CONNECTING,
    CLIENT_STATE_CONNECTED,
    CLIENT_STATE_IDLE,              //  等待用户输入
    CLIENT_STATE_SENDING,
    CLIENT_STATE_RECEIVING,
    CLIENT_STATE_PROCESSING,
    CLIENT_STATE_REGISTER_SUCCESS,
    CLIENT_STATE_REGISTER_FAILURE,
    CLIENT_STATE_LOGIN_SUCCESS,
    CLIENT_STATE_LOGIN_FAILURE,
    CLIENT_STATE_DELETE_SUCCESS,
    CLIENT_STATE_DELETE_FAILURE
} ClientState;

typedef struct ClientContext {
    int sockfd;
    ClientState state;
    char username[CLIENT_USERNAME_LEN];
    char cwd[CLIENT_PATH_LEN];
} ClientContext;

int client_context_init(ClientContext *context);

#endif
