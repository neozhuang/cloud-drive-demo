#include "server/dao_transfer.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "server/dao_status.h"

int dao_link_existing_file(database_pool_t *db_pool,
                                 uint64_t user_id,
                                 const char *target_path,
                                 uint64_t parent_id,
                                 const char *file_name,
                                 uint64_t file_id)
{
    MYSQL *conn;
    char escaped_path[PATH_MAX * 2 + 1];
    char escaped_name[NAME_MAX * 2 + 1];
    char sql[10240];

    conn = database_pool_acquire(db_pool);
    if (conn == NULL) {
        return -1;
    }

    mysql_real_escape_string(conn, escaped_path, target_path,
                             strlen(target_path));
    mysql_real_escape_string(conn, escaped_name, file_name,
                             strlen(file_name));

    if (mysql_query(conn, "START TRANSACTION") != 0) {
        database_pool_release(db_pool, conn);
        return -1;
    }

    snprintf(sql, sizeof(sql),
             "UPDATE files SET refs = refs + 1 WHERE id = %llu",
             (unsigned long long)file_id);

    if (mysql_query(conn, sql) != 0) {
        mysql_query(conn, "ROLLBACK");
        database_pool_release(db_pool, conn);
        return -1;
    }

    snprintf(sql, sizeof(sql),
             "INSERT INTO paths(user_id, path, file_id, parent_id, file_name, type) "
             "VALUES(%llu, '%s', %llu, %llu, '%s', 0)",
             (unsigned long long)user_id,
             escaped_path,
             (unsigned long long)file_id,
             (unsigned long long)parent_id,
             escaped_name);

    if (mysql_query(conn, sql) != 0) {
        mysql_query(conn, "ROLLBACK");
        database_pool_release(db_pool, conn);
        return -1;
    }

    if (mysql_query(conn, "COMMIT") != 0) {
        mysql_query(conn, "ROLLBACK");
        database_pool_release(db_pool, conn);
        return -1;
    }

    database_pool_release(db_pool, conn);
    return 0;
}

int dao_create_file_with_path(database_pool_t *db_pool,
                                    uint64_t user_id,
                                    const char *target_path,
                                    uint64_t parent_id,
                                    const char *file_name,
                                    const char *sha256_hex,
                                    uint64_t file_size)
{
    MYSQL *conn;
    my_ulonglong file_id;
    char escaped_path[PATH_MAX * 2 + 1];
    char escaped_name[NAME_MAX * 2 + 1];
    char sql[10240];

    conn = database_pool_acquire(db_pool);
    if (conn == NULL) {
        return -1;
    }

    mysql_real_escape_string(conn, escaped_path, target_path,
                             strlen(target_path));
    mysql_real_escape_string(conn, escaped_name, file_name,
                             strlen(file_name));

    if (mysql_query(conn, "START TRANSACTION") != 0) {
        database_pool_release(db_pool, conn);
        return -1;
    }

    snprintf(sql, sizeof(sql),
             "INSERT INTO files(hash, size, refs) "
             "VALUES(UNHEX('%s'), %llu, 1) "
             "ON DUPLICATE KEY UPDATE refs = refs + 1, id = LAST_INSERT_ID(id)",
             sha256_hex,
             (unsigned long long)file_size);

    if (mysql_query(conn, sql) != 0) {
        mysql_query(conn, "ROLLBACK");
        database_pool_release(db_pool, conn);
        return -1;
    }

    file_id = mysql_insert_id(conn);
    if (file_id == 0) {
        mysql_query(conn, "ROLLBACK");
        database_pool_release(db_pool, conn);
        return -1;
    }

    snprintf(sql, sizeof(sql),
             "INSERT INTO paths(user_id, path, file_id, parent_id, file_name, type) "
             "VALUES(%llu, '%s', %llu, %llu, '%s', 0)",
             (unsigned long long)user_id,
             escaped_path,
             (unsigned long long)file_id,
             (unsigned long long)parent_id,
             escaped_name);

    if (mysql_query(conn, sql) != 0) {
        mysql_query(conn, "ROLLBACK");
        database_pool_release(db_pool, conn);
        return -1;
    }

    if (mysql_query(conn, "COMMIT") != 0) {
        mysql_query(conn, "ROLLBACK");
        database_pool_release(db_pool, conn);
        return -1;
    }

    database_pool_release(db_pool, conn);
    return 0;
}

int dao_get_file_meta(database_pool_t* db_pool, 
                      uint64_t user_id,
                      const char* target_path,
                      file_meta_t* file_meta)
{
    char sql[1024];
    MYSQL_RES* res;
    MYSQL_ROW row;
    int file_type;

    snprintf(sql, sizeof(sql), 
             "SELECT p.id, p.file_id, p.file_name, p.type, f.size,"
             "LOWER(HEX(f.hash)) AS hash_hex FROM paths p "
             "LEFT JOIN files f ON p.file_id = f.id "
             "WHERE p.user_id = %lu AND p.path = '%s' "
             "LIMIT 1", user_id, target_path);

    if (database_pool_query(db_pool, sql, &res) != 0) {
        return DAO_DB_ERROR;
    }

    if (mysql_num_rows(res) == 0) {
        mysql_free_result(res);
        return DAO_NOT_FOUND;
    }

    row = mysql_fetch_row(res);
    if (row == NULL) {
        mysql_free_result(res);
        return DAO_DB_ERROR;
    }

    file_meta->path_id = strtoull(row[0], NULL, 10);
    file_meta->file_id = strtoull(row[1], NULL, 10);
    strncpy(file_meta->file_name, row[2], sizeof(file_meta->file_name) - 1);
    file_meta->file_name[sizeof(file_meta->file_name) - 1] = '\0';
    file_type = strtol(row[3], NULL, 10);
    file_meta->size = strtoull(row[4], NULL, 10);
    strncpy(file_meta->sha256_hex, row[5], sizeof(file_meta->sha256_hex) - 1);
    file_meta->file_name[sizeof(file_meta->file_name) - 1] = '\0';

    mysql_free_result(res);

    if (file_type == 1) {
        return DAO_TYPE_MISMATCH;
    }

    return DAO_OK;
}
