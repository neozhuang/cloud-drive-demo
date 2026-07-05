#include "server/dao_auth.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int dao_auth_register(database_pool_t *db_pool,
                                const char *username,
                                const char *password_hash)
{
    MYSQL *conn = database_pool_acquire(db_pool);
    if (conn == NULL) {
        return -1;
    }

    if (mysql_query(conn, "START TRANSACTION") != 0) {
        database_pool_release(db_pool, conn);
        return -1;
    }

    char sql[1024];

    snprintf(sql, sizeof(sql),
             "INSERT INTO users(username, password_hash) "
             "VALUES('%s', '%s')",
             username, password_hash);

    if (mysql_query(conn, sql) != 0) {
        mysql_query(conn, "ROLLBACK");
        database_pool_release(db_pool, conn);
        return -1;
    }

    unsigned long long user_id = mysql_insert_id(conn);

    snprintf(sql, sizeof(sql),
             "INSERT INTO paths(user_id, path, file_id, parent_id, file_name, type) "
             "VALUES(%llu, '/', NULL, NULL, '/', 1)",
             user_id);

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

int dao_auth_get_login_info(database_pool_t *db_pool,
                                   const char *username,
                                   login_info_t *out)
{
    char sql[1024];
    MYSQL_RES *res;
    MYSQL_ROW row;

    if (username == NULL || out == NULL) {
        return -1;
    }

    memset(out, 0, sizeof(*out));

    snprintf(sql, sizeof(sql),
             "SELECT u.id, u.password_hash, p.id "
             "FROM users u "
             "JOIN paths p ON p.user_id = u.id "
             "WHERE u.username = '%s' AND p.path = '/' "
             "LIMIT 1",
             username);

    if (database_pool_query(db_pool, sql, &res) != 0) {
        return -1;
    }

    if (mysql_num_rows(res) == 0) {
        mysql_free_result(res);
        return 1;
    }

    row = mysql_fetch_row(res);
    if (row == NULL || row[0] == NULL || row[1] == NULL || row[2] == NULL) {
        mysql_free_result(res);
        return -1;
    }

    out->user_id = strtoull(row[0], NULL, 10);
    out->root_path_id = strtoull(row[2], NULL, 10);

    if (strlen(row[1]) >= sizeof(out->password_hash)) {
        mysql_free_result(res);
        return -1;
    }

    strncpy(out->password_hash, row[1], sizeof(out->password_hash) - 1);
    out->password_hash[sizeof(out->password_hash) - 1] = '\0';

    mysql_free_result(res);
    return 0;
}
