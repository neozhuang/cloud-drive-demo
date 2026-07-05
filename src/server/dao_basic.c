#include "server/dao_basic.h"
#include "server/database_pool.h"

#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "server/dao_status.h"

/*
 * Find a virtual path for one user.
 *
 * Returns:
 *   DAO_PATH_IS_FILE    path exists and type == file; *path_id is set
 *   DAO_PATH_IS_DIR     path exists and type == directory; *path_id is set
 *   DAO_PATH_NOT_FOUND  no row exists for (user_id, virtual_path)
 *   DAO_PATH_DB_ERROR   MySQL/query/row parse error
 *   DAO_PATH_BAD_ARG    invalid input pointer
 */
int dao_path_get_node_id(database_pool_t* db_pool, uint64_t user_id, const char* virtual_path, uint64_t* path_id)
{
    char sql[1024];
    MYSQL_RES* res;
    MYSQL_ROW row;
    int file_type;

    snprintf(sql, sizeof(sql),
             "SELECT id, type FROM paths WHERE user_id = %lu AND path = '%s'",
             user_id, virtual_path);
    if (database_pool_query(db_pool, sql, &res) != 0) {
        return DAO_PATH_DB_ERROR;
    }

    if (mysql_num_rows(res) == 0) {
        mysql_free_result(res);
        return DAO_PATH_NOT_FOUND;
    }

    row = mysql_fetch_row(res);
    if (row == NULL || row[0] == NULL || row[1] == NULL) {
        mysql_free_result(res);
        return DAO_PATH_DB_ERROR;
    }

    *path_id = strtoull(row[0], NULL, 10);
    file_type = strtol(row[1], NULL, 10);
    mysql_free_result(res);

    if (file_type == 0) {
        return DAO_PATH_IS_FILE;
    }

    if (file_type == 1) {
        return DAO_PATH_IS_DIR;
    }

    return DAO_PATH_DB_ERROR;
}

int dao_path_insert_dir(database_pool_t* db_pool,uint64_t user_id, const char* target_path, uint64_t parent_id, const char* file_name)
{
    char sql[1024];

    snprintf(sql, sizeof(sql),
             "INSERT INTO paths(user_id, path, file_id, parent_id, file_name, type) "
             "VALUES(%lu, '%s', NULL, %lu, '%s', 1)",
             user_id, target_path, parent_id, file_name);

    return database_pool_execute(db_pool, sql);
}

int dao_path_dir_has_child(database_pool_t* db_pool, uint64_t user_id, uint64_t parent_id)
{
    char sql[1024];
    MYSQL_RES* res;
    int ret;

    snprintf(sql, sizeof(sql),
             "SELECT * FROM paths WHERE user_id = %lu AND parent_id = %lu",
             user_id, parent_id);

    if (database_pool_query(db_pool, sql, &res) != 0) {
        return -1;
    }
    ret = mysql_num_rows(res);
    mysql_free_result(res);

    return ret;
}

int dao_path_rmdir(database_pool_t* db_pool, uint64_t target_dir_id)
{
    char sql[1024];

    snprintf(sql, sizeof(sql),
             "DELETE FROM paths WHERE id = %lu",
             target_dir_id);

    return database_pool_execute(db_pool, sql);
}

int dao_path_get_ls_dir(database_pool_t* db_pool, uint64_t user_id, uint64_t parent_id, char* ls_resp, int size)
{
    char sql[1024];
    MYSQL_RES* res;
    MYSQL_ROW row;
    int ret;

    snprintf(sql, sizeof(sql),
             "SELECT file_name, type FROM paths WHERE user_id = %lu AND parent_id = %lu",
             user_id, parent_id);

    if (database_pool_query(db_pool, sql, &res) != 0) {
        return -1;
    }
    ret = mysql_num_rows(res);

    int used = 0;
    while ((row = mysql_fetch_row(res)) != NULL) {
        if (strtol(row[1], NULL, 10) == 1) {
            // dir
            used += snprintf(ls_resp + used, size - used, "%s/  ", row[0]);
        } 

        if (strtol(row[1], NULL, 10) == 0) {
            // file
            used += snprintf(ls_resp + used, size - used, "%s  ", row[0]);
        } 

        if (used >= size) {
            ls_resp[size - 1] = '\0';
            break;
        }
    }

    mysql_free_result(res);
    return ret;
}


dao_status_t dao_rm_file(database_pool_t* db_pool, uint64_t user_id, 
                     uint64_t path_id, char* hash_out, size_t hash_out_size)
{
    MYSQL* conn;
    MYSQL_RES* res;
    MYSQL_ROW row;
    char sql[1024];
    uint64_t file_id;
    int refs;
    int should_delete_file = 0;

    hash_out[0] = '\0';

    conn = database_pool_acquire(db_pool);
    if (conn == NULL) {
        return DAO_DB_ERROR;
    }

    if (mysql_query(conn, "START TRANSACTION") != 0) {
        database_pool_release(db_pool, conn);
        return DAO_DB_ERROR;
    }

    snprintf(sql, sizeof(sql),
             "SELECT file_id FROM paths "
             "WHERE id = %lu AND user_id = %lu AND type = 0",
             path_id, user_id);

    if (mysql_query(conn, sql) != 0) {
        mysql_query(conn, "ROLLBACK");
        database_pool_release(db_pool, conn);
        return DAO_DB_ERROR;
    }

    res = mysql_store_result(conn);
    if (res == NULL) {
        mysql_query(conn, "ROLLBACK");
        database_pool_release(db_pool, conn);
        return DAO_DB_ERROR;
    }

    row = mysql_fetch_row(res);
    if (row == NULL || row[0] == NULL) {
        mysql_free_result(res);
        mysql_query(conn, "ROLLBACK");
        database_pool_release(db_pool, conn);
        return DAO_NOT_FOUND;
    }

    file_id = strtoull(row[0], NULL, 10);
    mysql_free_result(res);

    snprintf(sql, sizeof(sql),
             "DELETE FROM paths "
             "WHERE id = %lu AND user_id = %lu AND type = 0",
             path_id, user_id);

    if (mysql_query(conn, sql) != 0) {
        mysql_query(conn, "ROLLBACK");
        database_pool_release(db_pool, conn);
        return DAO_DB_ERROR;
    }

    snprintf(sql, sizeof(sql),
             "UPDATE files SET refs = refs - 1 "
             "WHERE id = %lu AND refs > 0",
             file_id);

    if (mysql_query(conn, sql) != 0) {
        mysql_query(conn, "ROLLBACK");
        database_pool_release(db_pool, conn);
        return DAO_DB_ERROR;
    }

    snprintf(sql, sizeof(sql),
             "SELECT refs, LOWER(HEX(hash)) FROM files WHERE id = %lu",
             file_id);

    if (mysql_query(conn, sql) != 0) {
        mysql_query(conn, "ROLLBACK");
        database_pool_release(db_pool, conn);
        return DAO_DB_ERROR;
    }

    res = mysql_store_result(conn);
    if (res == NULL) {
        mysql_query(conn, "ROLLBACK");
        database_pool_release(db_pool, conn);
        return DAO_DB_ERROR;
    }

    row = mysql_fetch_row(res);
    if (row == NULL || row[0] == NULL || row[1] == NULL) {
        mysql_free_result(res);
        mysql_query(conn, "ROLLBACK");
        database_pool_release(db_pool, conn);
        return DAO_DB_ERROR;
    }

    refs = strtol(row[0], NULL, 10);

    if (refs == 0) {
        strncpy(hash_out, row[1], hash_out_size - 1);
        hash_out[hash_out_size - 1] = '\0';
        should_delete_file = 1;
    }

    mysql_free_result(res);

    if (should_delete_file) {
        snprintf(sql, sizeof(sql),
                 "DELETE FROM files WHERE id = %lu AND refs = 0",
                 file_id);

        if (mysql_query(conn, sql) != 0) {
            mysql_query(conn, "ROLLBACK");
            database_pool_release(db_pool, conn);
            return DAO_DB_ERROR;
        }
    }

    if (mysql_query(conn, "COMMIT") != 0) {
        mysql_query(conn, "ROLLBACK");
        database_pool_release(db_pool, conn);
        return DAO_DB_ERROR;
    }

    database_pool_release(db_pool, conn);

    return should_delete_file ? DAO_SHOULD_DELETE_PHYSICAL : DAO_OK;
}


dao_status_t dao_file_find_by_hash(database_pool_t *db_pool,
                                const char *sha256_hex,
                                uint64_t file_size,
                                uint64_t *file_id)
{
    char sql[1024];
    MYSQL_RES *res = NULL;
    MYSQL_ROW row;

    snprintf(sql, sizeof(sql),
             "SELECT id FROM files "
             "WHERE hash = UNHEX('%s') AND size = %llu "
             "LIMIT 1",
             sha256_hex,
             (unsigned long long)file_size);

    if (database_pool_query(db_pool, sql, &res) != 0) {
        return -1;
    }

    if (mysql_num_rows(res) == 0) {
        mysql_free_result(res);
        return 0;
    }

    row = mysql_fetch_row(res);
    if (row == NULL || row[0] == NULL) {
        mysql_free_result(res);
        return -1;
    }

    *file_id = strtoull(row[0], NULL, 10);
    mysql_free_result(res);
    return 1;
}

