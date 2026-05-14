#ifndef CLOUD_DRIVE_THREAD_POOL_H
#define CLOUD_DRIVE_THREAD_POOL_H

#include <pthread.h>

#include <mysql/mysql.h>

#include "config/config.h"

typedef void (*ThreadPoolTaskFn)(void *arg, MYSQL *mysql);

typedef struct ThreadPoolWorker ThreadPoolWorker;

typedef struct ThreadPoolTask {
    ThreadPoolTaskFn fn;
    void *arg;
} ThreadPoolTask;

typedef struct ThreadPool {
    pthread_t *threads;
    ThreadPoolWorker *workers;
    MYSQL **mysql_connections;
    ThreadPoolTask *tasks;
    int thread_count;
    int queue_capacity;
    int queue_size;
    int queue_head;
    int queue_tail;
    int stop;
    int started_workers;
    int failed_workers;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    pthread_cond_t workers_ready;
} ThreadPool;

int thread_pool_init(ThreadPool *pool, int thread_count, int queue_capacity,
                     const MysqlConfig *mysql_config);
int thread_pool_submit(ThreadPool *pool, ThreadPoolTaskFn fn, void *arg);
void thread_pool_destroy(ThreadPool *pool);

#endif
