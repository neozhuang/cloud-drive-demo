#pragma once

#include <stdint.h>

#include "server/database_pool.h"

typedef struct {
    uint64_t path_id;
    uint64_t file_id;
    char file_name[NAME_MAX];
    uint64_t size;
    char sha256_hex[65];
} file_meta_t;

int dao_link_existing_file(database_pool_t *db_pool,
                                 uint64_t user_id,
                                 const char *target_path,
                                 uint64_t parent_id,
                                 const char *file_name,
                                 uint64_t file_id);

int dao_create_file_with_path(database_pool_t *db_pool,
                                    uint64_t user_id,
                                    const char *target_path,
                                    uint64_t parent_id,
                                    const char *file_name,
                                    const char *sha256_hex,
                                    uint64_t file_size);

int dao_get_file_meta(database_pool_t* db_pool, 
                      uint64_t user_id,
                      const char* target_path,
                      file_meta_t* file_meta);

