#pragma once

#include "client/state.h"

enum {
    AUTH_OK = 0,
    AUTH_RETRY = 1,
    AUTH_EXIT = 2
};

int user_auth(client_state_t *client_state);
int user_login(client_state_t *client_state);
int user_register(client_state_t *client_state);
