#include "../include/threadpool.h"
#include "../include/do_task.h"

//每一个子线程在执行的函数执行体(start_routine)
void * thread_func(void* arg) {
    threadpool_t * p_threadpool = (threadpool_t*)arg;
    //工作循环（永远待命）
    while(1) {
        node_t* p_node = queue_deque(&p_threadpool->queue);
        if(p_node){
            // 执行业务命令
            do_task(p_node);
            free(p_node);   // 执行完任务后，释放节点
        }
        else {   // p_node == NULL
            break;
        }
    }
    printf("sub thread %ld is exiting.\n", pthread_self());
    return NULL;
}


int threadpool_init(threadpool_t * p_threadpool, int num) {
    p_threadpool->p_threads = (pthread_t *)calloc(num, sizeof(pthread_t));
    if(p_threadpool->p_threads == NULL) return -1;
    p_threadpool->thread_count = num;
    if(queue_init(&p_threadpool->queue)) {
        free(p_threadpool->p_threads);
        return -1;
    }
    return 0;
}

int threadpool_destroy(threadpool_t * p_threadpool) {
    free(p_threadpool->p_threads);
    queue_destroy(&p_threadpool->queue);
    return 0;
}


int threadpool_start(threadpool_t * p_threadpool) {
    int i, ret;
    for(i = 0; i < p_threadpool->thread_count; ++i){
        ret = pthread_create(&p_threadpool->p_threads[i], NULL, thread_func, p_threadpool);
        THREAD_ERROR_CHECK(ret, "pthread_create");
    }
    return 0;
}

int threadpool_stop(threadpool_t * p_threadpool) {
    // 如果任务队列中还有任务，则阻塞当前线程，直到任务队列为空
    while(queue_is_empty(&p_threadpool->queue) == 0){
        sleep(1);
    }
    p_threadpool->queue.shutdown = 1;//关闭线程池
    int ret = pthread_cond_broadcast(&p_threadpool->queue.cond);//唤醒所有等待的线程，让它们结束任务
    THREAD_ERROR_CHECK(ret, "pthread_cond_broadcast");
    // 回收线程池中的线程
    for(int i = 0; i < p_threadpool->thread_count; ++i){
        pthread_join(p_threadpool->p_threads[i], NULL);
    }
    return 0;
}
