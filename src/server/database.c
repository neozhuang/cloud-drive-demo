#include "server/database.h"

#include <mysql/mysql.h>
#include <stdio.h>

#include "common/log.h"

// [mysql]
// host = localhost
// port = 3306
// user = test
// password = 123456
// database = cloud_drive_demo
// charset = utf8mb4
// pool_size = 4
int database_init(const char* host, const char* user, const char* passwd, 
                  const char* db, unsigned int port, const char* charset) {
    MYSQL mysql;
    mysql_init(&mysql);

    if (mysql_real_connect(&mysql, host, user, passwd, NULL, port, NULL, 0) == NULL) {
        LOG_ERROR("Failed to connect mysql: %s", mysql_error(&mysql));
        mysql_close(&mysql);
        return -1;
    }

    // Create database if not exists and set charset
    char query[256];
    snprintf(query, sizeof(query), 
             "CREATE DATABASE IF NOT EXISTS %s DEFAULT CHARACTER SET %s", 
             db, charset);
    if (mysql_query(&mysql, query)) {
        LOG_ERROR("Failed to create database: %s", mysql_error(&mysql));
        mysql_close(&mysql);
        return -1;
    }

    // Select the created/existed database
    if (mysql_select_db(&mysql, db)) {
        LOG_ERROR("Failed to select database: %s", mysql_error(&mysql));
        mysql_close(&mysql);
        return -1;
    }

    // Define the statement of creating tables
    // users: store user auth info
    // files: store the real file meta data
    // paths: store the logical file paths tree
    const char *create_users_table = 
        "CREATE TABLE IF NOT EXISTS users ("
        "id INT AUTO_INCREMENT PRIMARY KEY, "
        "username VARCHAR(64) NOT NULL UNIQUE, "
        "password_hash VARCHAR(384) NOT NULL"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    const char *create_files_table = 
        "CREATE TABLE IF NOT EXISTS files ("
        "id BIGINT AUTO_INCREMENT PRIMARY KEY, "
        "hash BINARY(32) NOT NULL UNIQUE, "
        "size BIGINT, "
        "refs INT NOT NULL DEFAULT 0"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    const char *create_paths_table = 
        "CREATE TABLE IF NOT EXISTS paths ("
        "id BIGINT AUTO_INCREMENT PRIMARY KEY, "
        "user_id INT NOT NULL, "
        "path VARCHAR(768) NOT NULL, "
        "file_id BIGINT NULL, "
        "parent_id BIGINT, "
        "file_name VARCHAR(255) NOT NULL, "
        "type TINYINT NOT NULL, "
        "UNIQUE KEY (user_id, path), "
        "INDEX idx_user_parent (user_id, parent_id)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    // Execute to create tables, if any table is failed to create, then stop.
    if (mysql_query(&mysql, create_users_table)) {
        LOG_ERROR("Failed to create table users: %s", mysql_error(&mysql));
        mysql_close(&mysql);
        return -1;
    }
    
    if (mysql_query(&mysql, create_files_table)) {
        LOG_ERROR("Failed to create table files: %s", mysql_error(&mysql));
        mysql_close(&mysql);
        return -1;
    }

    if (mysql_query(&mysql, create_paths_table)) {
        LOG_ERROR("Failed to create table paths: ", mysql_error(&mysql));
        mysql_close(&mysql);
        return -1;
    }

    LOG_INFO("Completed to initialize database [%s] and its tables", db);

    mysql_close(&mysql);
    return 0;
}
