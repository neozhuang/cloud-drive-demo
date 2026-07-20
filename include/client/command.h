#pragma once

#include "client/runtime.h"

typedef enum {
    CLIENT_COMMAND_OK = 0,
    CLIENT_COMMAND_EXIT,
    CLIENT_COMMAND_ERROR
} client_command_result_t;

client_command_result_t client_command_execute(client_runtime_t *runtime,
                                               const char *input);
