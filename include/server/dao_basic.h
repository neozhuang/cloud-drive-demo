#pragma once

#include <stdint.h>

#include "server/dao_status.h"
#include "server/database_pool.h"

typedef enum {
    DAO_PATH_IS_FILE = 0,
    DAO_PATH_IS_DIR = 1,
    DAO_PATH_NOT_FOUND = 2,
    DAO_PATH_DB_ERROR = -1,
    DAO_PATH_BAD_ARG = -2
} dao_path_result_t;

int dao_path_get_node_id(database_pool_t* db_pool, uint64_t user_id, const char* virtual_path, uint64_t* path_id);

int dao_path_insert_dir(database_pool_t* db_pool,uint64_t user_id, const char* target_path, uint64_t parent_id, const char* file_name);

int dao_path_dir_has_child(database_pool_t* db_pool, uint64_t user_id, uint64_t parent_id);

int dao_path_rmdir(database_pool_t* db_pool, uint64_t target_dir_id);

int dao_path_get_ls_dir(database_pool_t* db_pool, uint64_t user_id, uint64_t parent_id, char* ls_resp, int size);

dao_status_t dao_rm_file(database_pool_t* db_pool, uint64_t user_id,
                     uint64_t path_id, char* hash_out, size_t hash_out_size);




dao_status_t dao_file_find_by_hash(database_pool_t *db_pool,
                                const char *sha256_hex,
                                uint64_t file_size,
                                uint64_t *file_id);
