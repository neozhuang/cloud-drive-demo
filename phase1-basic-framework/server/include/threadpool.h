#ifndef __THREADPOOL_H__
#define __THREADPOOL_H__

#include "block_queue.h"
#include "common.h"

typedef struct threadpool_s{
    pthread_t * p_threads;
    int thread_count;
    block_queue_t queue;
}threadpool_t;

int threadpool_init(threadpool_t * p_threadpool, int num);
int threadpool_destroy(threadpool_t * p_threadpool);
int threadpool_start(threadpool_t * p_threadpool);
int threadpool_stop(threadpool_t * p_threadpool);

#endif /* __THREADPOOL_H__ */



