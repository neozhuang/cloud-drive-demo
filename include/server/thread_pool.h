#pragma once

/**
 * Abstract Data Type
 *
 * How to design and define a new data type, 
 * specifically including establishing abstractions,
 * creating interfaces, and implementing interfaces.
 *
 * control the degree of separation between interface and implementation
 * what is the interface, what is the implementation.
 */

typedef struct thread_pool thread_pool_t;

/**
 * thread_pool_create
 *
 * create thread pool, the returned pointer points to 
 * the malloced meomory in free space, and in the func, continues to malloc meomory to pthread_t.
 *
 * So the overview of relations is:
 * pool in the main
 *       |
 *       v
 *  thread_pool_t object in free space
 *   |
 *   +--> pool->threads pointer 
 *           |
 *           v
 *       pthread_t array in free space
 */
thread_pool_t *thread_pool_create(int thread_num, int queue_capacity);

/**
 * thread_pool_add
 *
 * add new task to task queue in the thread pool
 */
int thread_pool_add(thread_pool_t *pool, void (*function)(void *), void *arg);

/**
 * thread_pool_destroy
 *
 * destory thread pool, free meomory malloced for pthread_t array,
 * destory mutex, condition, free memory malloced for thread_pool_t.
 */
void thread_pool_destroy(thread_pool_t *pool);
