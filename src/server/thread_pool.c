/*
 * What is the thread pool?
 * The core idea is Producer-Consumer pattern.
 * To achieve high performance, the task should be isolated with the connection.
 * That means the task at the task queue 
 *      should be the packet rather than the client fd.
 *
 * So, keep it simple.
 */

#include "server/thread_pool.h"

#include <pthread.h>
#include <stdlib.h>

typedef struct task {
    void (*function)(void*);
    void* arg;
    struct task* next;
} task_t;

typedef struct task_queue {
    task_t* head;
    task_t* tail;
    int size;
    int capacity;
} task_queue_t;

struct thread_pool {
    pthread_t* threads;
    int thread_num;

    task_queue_t queue;

    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;

    int shutdown;
};

static void* thread_worker(void* arg)
{
    thread_pool_t* pool = arg;

    while (1) {
        pthread_mutex_lock(&pool->lock);

        while (pool->queue.size == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->not_empty, &pool->lock);
        }

        if (pool->shutdown == 1 && pool->queue.size == 0) {
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        task_t* task = pool->queue.head;
        pool->queue.head = task->next;
        if (pool->queue.head == NULL) {
            pool->queue.tail = NULL;
        }
        pool->queue.size--;

        pthread_cond_signal(&pool->not_full);

        pthread_mutex_unlock(&pool->lock);

        task->function(task->arg);
        free(task);
    }

    return NULL;
}


thread_pool_t *thread_pool_create(int thread_num, int queue_capacity)
{
    if (thread_num <= 0 || queue_capacity <= 0) {
        return NULL;
    }

    thread_pool_t* pool = calloc(1, sizeof(thread_pool_t));
    if (pool == NULL) {
        return NULL;
    }

    pool->thread_num = thread_num;
    pool->queue.capacity = queue_capacity;

    pool->threads = calloc(pool->thread_num, sizeof(pthread_t));
    if (pool->threads == NULL) {
        free(pool);
        return NULL; 
    }

    if (pthread_mutex_init(&pool->lock, NULL) != 0) {
        free(pool->threads);
        free(pool);
        return NULL;
    }

    if (pthread_cond_init(&pool->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&pool->lock);
        free(pool->threads);
        free(pool);
        return NULL;
    }

    if (pthread_cond_init(&pool->not_full, NULL) != 0) {
        pthread_cond_destroy(&pool->not_empty);
        pthread_mutex_destroy(&pool->lock);
        free(pool->threads);
        free(pool);
        return NULL;
    }

    for (int i = 0; i < thread_num; i++) {
        if (pthread_create(&pool->threads[i], NULL, thread_worker, pool) != 0) {
            pthread_mutex_lock(&pool->lock);
            pool->shutdown = 1;
            pthread_cond_broadcast(&pool->not_empty);
            pthread_mutex_unlock(&pool->lock);

            for (int j = 0; j < i; j++) {
                pthread_join(pool->threads[j], NULL);
            }
            
            pthread_cond_destroy(&pool->not_empty);
            pthread_cond_destroy(&pool->not_full);
            pthread_mutex_destroy(&pool->lock);

            free(pool->threads);
            free(pool);
            return NULL;
        }
    }

    return pool;
}

int thread_pool_add(thread_pool_t *pool, void (*function)(void *), void *arg)
{
    if (pool == NULL || function == NULL) {
        return -1;
    }

    task_t* task = malloc(sizeof(task_t));
    if (task == NULL) {
        return -1;
    }

    task->function = function;
    task->arg = arg;
    task->next = NULL;

    pthread_mutex_lock(&pool->lock);
    
    while (pool->queue.size == pool->queue.capacity && !pool->shutdown) {
        pthread_cond_wait(&pool->not_full, &pool->lock);
    }

    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->lock);
        free(task);
        return -1;
    }

    if (pool->queue.tail == NULL) {
        pool->queue.head = task;
        pool->queue.tail = task;
    } else {
        pool->queue.tail->next = task;
        pool->queue.tail = task;
    }

    pool->queue.size++;

    pthread_cond_signal(&pool->not_empty);

    pthread_mutex_unlock(&pool->lock);

    return 0;
}

void thread_pool_destroy(thread_pool_t *pool)
{
    if (pool == NULL) {
        return;
    }

    pthread_mutex_lock(&pool->lock);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->not_empty);
    pthread_cond_broadcast(&pool->not_full);
    pthread_mutex_unlock(&pool->lock);

    for (int i = 0; i < pool->thread_num; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    pthread_cond_destroy(&pool->not_empty);
    pthread_cond_destroy(&pool->not_full);
    pthread_mutex_destroy(&pool->lock);

    free(pool->threads);
    free(pool);
}
