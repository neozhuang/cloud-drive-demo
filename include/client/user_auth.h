#pragma once

#include "client/runtime.h"

enum {
    AUTH_OK = 0,
    AUTH_CONNECTION_ERROR = 1,
    AUTH_EXIT = 2
};

int user_auth(client_runtime_t *runtime);
int user_login(client_runtime_t *runtime);
int user_register(client_runtime_t *runtime);
