#pragma once

#include <stdint.h>
#include <crypt.h>

#include "server/database_pool.h"

typedef struct {
    uint64_t user_id;
    char password_hash[CRYPT_OUTPUT_SIZE];
    uint64_t root_path_id;
} login_info_t;

int dao_auth_register(database_pool_t *db_pool,
                                const char *username,
                                const char *password_hash);

int dao_auth_get_login_info(database_pool_t *pool,
                               const char *username,
                               login_info_t *out);
