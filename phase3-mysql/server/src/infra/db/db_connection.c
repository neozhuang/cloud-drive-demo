#include "infra/db/db_connection.h"

#include <errno.h>
#include <stdio.h>

MYSQL *db_connect(const MysqlConfig *config)
{
    MYSQL *mysql;
    unsigned int timeout_seconds = 5;

    if (config == NULL) {
        errno = EINVAL;
        return NULL;
    }

    mysql = mysql_init(NULL);
    if (mysql == NULL) {
        return NULL;
    }

    mysql_options(mysql, MYSQL_SET_CHARSET_NAME, config->charset);
    mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout_seconds);
    mysql_options(mysql, MYSQL_OPT_READ_TIMEOUT, &timeout_seconds);
    mysql_options(mysql, MYSQL_OPT_WRITE_TIMEOUT, &timeout_seconds);

    if (mysql_real_connect(mysql, config->host, config->user, config->password,
                           config->database, (unsigned int)config->port, NULL,
                           0) == NULL) {
        fprintf(stderr, "Failed to connect MySQL %s:%d/%s: %s\n", config->host,
                config->port, config->database, mysql_error(mysql));
        mysql_close(mysql);
        return NULL;
    }

    return mysql;
}

int db_ensure_schema(const MysqlConfig *config)
{
    static const char *create_users_table_sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,"
        "username VARCHAR(64) NOT NULL UNIQUE,"
        "password_hash VARCHAR(255) NOT NULL,"
        "is_deleted TINYINT NOT NULL DEFAULT 0,"
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";
    static const char *create_files_table_sql =
        "CREATE TABLE IF NOT EXISTS files ("
        "id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,"
        "owner_id BIGINT UNSIGNED NOT NULL,"
        "parent_id BIGINT UNSIGNED NULL,"
        "file_name VARCHAR(255) NOT NULL,"
        "file_path VARCHAR(1024) NOT NULL,"
        "is_directory TINYINT(1) NOT NULL DEFAULT 0,"
        "file_size BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        "file_hash BINARY(16) NOT NULL,"
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "UNIQUE KEY uniq_user_path (user_id, path)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";
    MYSQL *mysql;

    mysql = db_connect(config);
    if (mysql == NULL) {
        return -1;
    }

    if (mysql_query(mysql, create_users_table_sql) != 0) {
        fprintf(stderr, "Failed to create users table: %s\n", mysql_error(mysql));
        db_close(mysql);
        return -1;
    }

    if (mysql_query(mysql, create_files_table_sql) != 0) {
        fprintf(stderr, "Failed to create files table: %s\n", mysql_error(mysql));
        db_close(mysql);
        return -1;
    }

    db_close(mysql);
    return 0;
}

int db_library_init(void)
{
    if (mysql_library_init(0, NULL, NULL) != 0) {
        fprintf(stderr, "Failed to initialize MySQL client library\n");
        return -1;
    }

    return 0;
}

void db_library_end(void)
{
    mysql_library_end();
}

void db_close(MYSQL *mysql)
{
    if (mysql != NULL) {
        mysql_close(mysql);
    }
}
