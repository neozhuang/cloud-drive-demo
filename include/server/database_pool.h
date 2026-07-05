#pragma once

#include <mysql/mysql.h>

#include "server/config.h"

typedef struct database_pool database_pool_t;

/*
 * Create a database connection pool from MySQL config.
 *
 * Returns a new pool on success, or NULL on failure.
 * The returned pool must be released with database_pool_destroy().
 */
database_pool_t* database_pool_create(const mysql_config_t *config);

/*
 * Destroy a database connection pool and close its connections.
 *
 * The pool must not be used after this call.
 */
void database_pool_destroy(database_pool_t *pool);

/*
 * Execute a SQL statement that does not return rows.
 *
 * Typical usage: INSERT, UPDATE, DELETE, CREATE TABLE.
 * Returns 0 on success, or -1 on failure.
 */
int database_pool_execute(database_pool_t *pool, const char *sql);

/*
 * Execute a SQL query and store the full result set.
 *
 * The caller owns *out_res on success and must call mysql_free_result().
 * Returns 0 on success, or -1 on failure.
 */
int database_pool_query(database_pool_t *pool, const char *sql, MYSQL_RES **out_res);

MYSQL *database_pool_acquire(database_pool_t *pool);
void database_pool_release(database_pool_t *pool, MYSQL *conn);
