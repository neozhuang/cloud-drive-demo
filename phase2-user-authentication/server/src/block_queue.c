#include "../include/block_queue.h"
#include "../include/common.h"

int queue_init(block_queue_t * que) {
    que->p_head = NULL;
    que->p_tail = NULL;
    que->size = 0;
    pthread_mutex_init(&que->mutex,NULL);
    pthread_cond_init(&que->cond,NULL);
    que->shutdown = 0;
    return 0;
}

int queue_destroy(block_queue_t * que) {
    node_t * p_cur = que->p_head;
    while(p_cur != NULL){
        node_t * p_node = p_cur;
        p_cur = p_cur->p_next;
        free(p_node);
    }
    pthread_mutex_destroy(&que->mutex);
    pthread_cond_destroy(&que->cond);
    return 0;   
}

int queue_is_empty(block_queue_t * que) {
    return que->size == 0;   
} 

int queue_get_size(block_queue_t * que) {
    return que->size;
}

int queue_enque(block_queue_t * que, node_t * p_node) {
    pthread_mutex_lock(&que->mutex);
    if(que->p_head == NULL)
        que->p_head = p_node;
    else
        que->p_tail->p_next = p_node;
    que->p_tail = p_node;
    que->size++;
    pthread_cond_signal(&que->cond);
    pthread_mutex_unlock(&que->mutex);
    return 0;
}

node_t * queue_deque(block_queue_t * que) {
    node_t * p_node = NULL;
    pthread_mutex_lock(&que->mutex);
    while(queue_is_empty(que) && que->shutdown == 0){
        pthread_cond_wait(&que->cond,&que->mutex);
    }
    if(que->shutdown == 1)
    {
        printf("I am worker, I am going to exit\n");
        pthread_mutex_unlock(&que->mutex);
        pthread_exit(NULL); // 退出当前线程
    }
    p_node = que->p_head;
    if(queue_get_size(que) == 1) que->p_head = que->p_tail = NULL;
    else que->p_head = p_node->p_next;
    que->size--;
    pthread_mutex_unlock(&que->mutex);
    return p_node;
}
