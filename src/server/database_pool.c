#include "server/database_pool.h"

#include <stdlib.h>
#include <pthread.h>
#include <mysql/mysql.h>
#include <mysql/mysqld_error.h>

#include "common/log.h"

struct database_pool {
    MYSQL **connections;
    int size;
    int available;

    pthread_mutex_t lock;
    pthread_cond_t not_empty;
};

static MYSQL *mysql_connect_one(const mysql_config_t *config)
{
    MYSQL *conn = mysql_init(NULL);
    if (conn == NULL) {
        return NULL;
    }

    if (mysql_real_connect(conn,
                           config->host,
                           config->user,
                           config->password,
                           config->database,
                           (unsigned int)atoi(config->port),
                           NULL,
                           0) == NULL) {
        LOG_ERROR("mysql_real_connect failed: %s", mysql_error(conn));
        mysql_close(conn);
        return NULL;
    }

    return conn;
}

MYSQL *database_pool_acquire(database_pool_t *pool)
{
    if (pool == NULL) {
        return NULL;
    }

    pthread_mutex_lock(&pool->lock);
    while (pool->available == 0) {
        pthread_cond_wait(&pool->not_empty, &pool->lock);
    }
    MYSQL *conn = pool->connections[--pool->available];
    pool->connections[pool->available] = NULL;
    pthread_mutex_unlock(&pool->lock);
    return conn;
}

void database_pool_release(database_pool_t *pool, MYSQL *conn)
{
    if (pool == NULL || conn == NULL) {
        return;
    }

    pthread_mutex_lock(&pool->lock);
    pool->connections[pool->available++] = conn;
    pthread_cond_signal(&pool->not_empty);
    pthread_mutex_unlock(&pool->lock);
}

database_pool_t* database_pool_create(const mysql_config_t *config)
{
    database_pool_t *pool = calloc(1, sizeof(*pool));
    if (pool == NULL) {
        return NULL;
    }

    pool->connections = calloc(config->pool_size, sizeof(*pool->connections));
    if (pool->connections == NULL) {
        free(pool);
        return NULL;
    }

    pool->size = config->pool_size;
    if (pthread_mutex_init(&pool->lock, NULL) != 0) {
        free(pool->connections);
        free(pool);
        return NULL;
    }
    if (pthread_cond_init(&pool->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&pool->lock);
        free(pool->connections);
        free(pool);
        return NULL;
    }

    for (int i = 0; i < pool->size; i++) {
        pool->connections[i] = mysql_connect_one(config);
        if (pool->connections[i] == NULL) {
            for (int j = 0; j < i; j++) {
                mysql_close(pool->connections[j]);
            }

            pthread_cond_destroy(&pool->not_empty);
            pthread_mutex_destroy(&pool->lock);
            free(pool->connections);
            free(pool);
            return NULL;
        }
        pool->available++;
    }

    return pool;
}

void database_pool_destroy(database_pool_t *pool)
{
    if (pool == NULL) {
        return;
    }

    for (int i = 0; i < pool->available; i++) {
        mysql_close(pool->connections[i]);
    }
    pthread_cond_destroy(&pool->not_empty);
    pthread_mutex_destroy(&pool->lock);
    free(pool->connections);
    free(pool);
}

int database_pool_execute(database_pool_t *pool, const char *sql)
{
    // get one conn, do sql, release conn
    MYSQL* conn = database_pool_acquire(pool);
    if (conn == NULL) {
        LOG_ERROR("Failed to get one connection from database pool");
        return -1;
    }
    int ret = mysql_query(conn, sql);
    if (ret != 0) {
        LOG_ERROR("Failed to execute sql statement: %s", mysql_error(conn));
    database_pool_release(pool, conn);
    if (mysql_errno(conn) == ER_DUP_ENTRY) {
        // duplicate row / unique key conflict
        return 1;
    }
    // other database error
        return -1;
    }
    database_pool_release(pool, conn);
    return 0;
}

int database_pool_query(database_pool_t *pool, const char *sql, MYSQL_RES **out_res)
{
    // get one conn, do sql, release conn
    MYSQL* conn = database_pool_acquire(pool);
    if (conn == NULL) {
        LOG_ERROR("Failed to get one connection from database pool");
        return -1;
    }
    int ret = mysql_query(conn, sql);
    if (ret != 0) {
        LOG_ERROR("Failed to execute sql statement: %s", mysql_error(conn));
        database_pool_release(pool, conn);
        return -1;
    }
    *out_res = mysql_store_result(conn);
    database_pool_release(pool, conn);
    return 0;
}
