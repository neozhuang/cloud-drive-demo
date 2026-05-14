#include "infra/thread_pool/thread_pool.h"

#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <mysql/mysql.h>

#include "infra/db/db_connection.h"

struct ThreadPoolWorker {
    ThreadPool *pool;
    int index;
    MysqlConfig mysql_config;
};

static void thread_pool_mark_worker_started(ThreadPool *pool, int failed)
{
    pthread_mutex_lock(&pool->mutex);
    if (failed) {
        pool->failed_workers += 1;
        pool->stop = 1;
        pthread_cond_broadcast(&pool->not_empty);
        pthread_cond_broadcast(&pool->not_full);
    } else {
        pool->started_workers += 1;
    }
    pthread_cond_signal(&pool->workers_ready);
    pthread_mutex_unlock(&pool->mutex);
}

static void *thread_pool_worker(void *arg)
{
    ThreadPoolWorker *worker = (ThreadPoolWorker *)arg;
    ThreadPool *pool = worker->pool;
    MYSQL *mysql;

    mysql_thread_init();
    mysql = db_connect(&worker->mysql_config);
    if (mysql == NULL) {
        fprintf(stderr, "Worker %d failed to initialize MySQL connection\n",
                worker->index);
        thread_pool_mark_worker_started(pool, 1);
        mysql_thread_end();
        return NULL;
    }

    pool->mysql_connections[worker->index] = mysql;
    thread_pool_mark_worker_started(pool, 0);

    for (;;) {
        ThreadPoolTask task;

        memset(&task, 0, sizeof(task));
        pthread_mutex_lock(&pool->mutex);
        while (pool->queue_size == 0 && !pool->stop) {
            pthread_cond_wait(&pool->not_empty, &pool->mutex);
        }

        if (pool->stop && pool->queue_size == 0) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        task = pool->tasks[pool->queue_head];
        pool->queue_head = (pool->queue_head + 1) % pool->queue_capacity;
        pool->queue_size -= 1;
        pthread_cond_signal(&pool->not_full);
        pthread_mutex_unlock(&pool->mutex);

        if (task.fn != NULL) {
            task.fn(task.arg, pool->mysql_connections[worker->index]);
        }
    }

    db_close(pool->mysql_connections[worker->index]);
    pool->mysql_connections[worker->index] = NULL;
    mysql_thread_end();
    return NULL;
}

static void thread_pool_reset(ThreadPool *pool)
{
    if (pool == NULL) {
        return;
    }

    pool->threads = NULL;
    pool->workers = NULL;
    pool->mysql_connections = NULL;
    pool->tasks = NULL;
    pool->thread_count = 0;
    pool->queue_capacity = 0;
    pool->queue_size = 0;
    pool->queue_head = 0;
    pool->queue_tail = 0;
    pool->stop = 0;
    pool->started_workers = 0;
    pool->failed_workers = 0;
}

int thread_pool_init(ThreadPool *pool, int thread_count, int queue_capacity,
                     const MysqlConfig *mysql_config)
{
    int index;
    int created_threads = 0;

    if (pool == NULL || thread_count <= 0 || queue_capacity <= 0 ||
        mysql_config == NULL) {
        errno = EINVAL;
        return -1;
    }

    thread_pool_reset(pool);
    pool->threads = (pthread_t *)calloc((size_t)thread_count, sizeof(pthread_t));
    pool->workers =
        (ThreadPoolWorker *)calloc((size_t)thread_count, sizeof(ThreadPoolWorker));
    pool->mysql_connections =
        (MYSQL **)calloc((size_t)thread_count, sizeof(MYSQL *));
    pool->tasks =
        (ThreadPoolTask *)calloc((size_t)queue_capacity, sizeof(ThreadPoolTask));
    if (pool->threads == NULL || pool->workers == NULL ||
        pool->mysql_connections == NULL || pool->tasks == NULL) {
        free(pool->threads);
        free(pool->workers);
        free(pool->mysql_connections);
        free(pool->tasks);
        thread_pool_reset(pool);
        return -1;
    }

    pool->thread_count = thread_count;
    pool->queue_capacity = queue_capacity;

    if (pthread_mutex_init(&pool->mutex, NULL) != 0 ||
        pthread_cond_init(&pool->not_empty, NULL) != 0 ||
        pthread_cond_init(&pool->not_full, NULL) != 0 ||
        pthread_cond_init(&pool->workers_ready, NULL) != 0) {
        pthread_mutex_destroy(&pool->mutex);
        pthread_cond_destroy(&pool->not_empty);
        pthread_cond_destroy(&pool->not_full);
        pthread_cond_destroy(&pool->workers_ready);
        free(pool->threads);
        free(pool->workers);
        free(pool->mysql_connections);
        free(pool->tasks);
        thread_pool_reset(pool);
        return -1;
    }

    for (index = 0; index < thread_count; ++index) {
        pool->workers[index].pool = pool;
        pool->workers[index].index = index;
        pool->workers[index].mysql_config = *mysql_config;
        if (pthread_create(&pool->threads[index], NULL, thread_pool_worker,
                           &pool->workers[index]) != 0) {
            pool->stop = 1;
            pthread_cond_broadcast(&pool->not_empty);
            while (--created_threads >= 0) {
                pthread_join(pool->threads[created_threads], NULL);
            }
            pthread_mutex_destroy(&pool->mutex);
            pthread_cond_destroy(&pool->not_empty);
            pthread_cond_destroy(&pool->not_full);
            pthread_cond_destroy(&pool->workers_ready);
            free(pool->threads);
            free(pool->workers);
            free(pool->mysql_connections);
            free(pool->tasks);
            thread_pool_reset(pool);
            return -1;
        }
        created_threads += 1;
    }

    pthread_mutex_lock(&pool->mutex);
    while (pool->started_workers + pool->failed_workers < pool->thread_count) {
        pthread_cond_wait(&pool->workers_ready, &pool->mutex);
    }

    if (pool->failed_workers > 0) {
        pthread_mutex_unlock(&pool->mutex);
        thread_pool_destroy(pool);
        return -1;
    }
    pthread_mutex_unlock(&pool->mutex);

    return 0;
}

int thread_pool_submit(ThreadPool *pool, ThreadPoolTaskFn fn, void *arg)
{
    if (pool == NULL || fn == NULL) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&pool->mutex);
    while (pool->queue_size == pool->queue_capacity && !pool->stop) {
        pthread_cond_wait(&pool->not_full, &pool->mutex);
    }

    if (pool->stop) {
        pthread_mutex_unlock(&pool->mutex);
        errno = ECANCELED;
        return -1;
    }

    pool->tasks[pool->queue_tail].fn = fn;
    pool->tasks[pool->queue_tail].arg = arg;
    pool->queue_tail = (pool->queue_tail + 1) % pool->queue_capacity;
    pool->queue_size += 1;
    pthread_cond_signal(&pool->not_empty);
    pthread_mutex_unlock(&pool->mutex);
    return 0;
}

void thread_pool_destroy(ThreadPool *pool)
{
    int index;

    if (pool == NULL || pool->threads == NULL) {
        return;
    }

    pthread_mutex_lock(&pool->mutex);
    pool->stop = 1;
    pthread_cond_broadcast(&pool->not_empty);
    pthread_cond_broadcast(&pool->not_full);
    pthread_mutex_unlock(&pool->mutex);

    for (index = 0; index < pool->thread_count; ++index) {
        pthread_join(pool->threads[index], NULL);
    }

    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->not_empty);
    pthread_cond_destroy(&pool->not_full);
    pthread_cond_destroy(&pool->workers_ready);
    free(pool->threads);
    free(pool->workers);
    free(pool->mysql_connections);
    free(pool->tasks);
    thread_pool_reset(pool);
}
