#ifndef __BLOCK_QUEUE_H__
#define __BLOCK_QUEUE_H__

#include <pthread.h>
#include <stdlib.h>
#include "packet.h"

typedef struct node_s {
    int netfd;                  // 与客户端进行通信的socket
    int epfd;                   // epoll实例
    cmd_type_t type;            // 命令类型
    char content[CONTENT_MAX];     // 命令参数 
    struct node_s * p_next;
} node_t;

typedef struct block_queue_s {
    node_t * p_head;
    node_t * p_tail;
    int size;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int shutdown; // 线程池关闭标志 0:未关闭 1:已关闭
}block_queue_t;

int queue_init(block_queue_t * que); // 初始化队列
int queue_destroy(block_queue_t * que); // 销毁队列
int queue_is_empty(block_queue_t * que); // 判断队列是否为空
int queue_get_size(block_queue_t * que); // 返回队列大小
int queue_enque(block_queue_t * que, node_t * p_node); // 入队
node_t * queue_deque(block_queue_t * que); // 出队

#endif /* __BLOCK_QUEUE_H__ */
