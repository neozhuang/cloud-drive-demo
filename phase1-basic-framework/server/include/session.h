#ifndef __SESSION_H__
#define __SESSION_H__

#include "packet.h"
#include <pthread.h>
#include <linux/limits.h>

#define MAX_SESSIONS 1024

typedef struct {
    int netfd;                              // 连接socket
    char current_path[PATH_MAX];            // 当前路径
} session_t;

// 初始化会话管理系统
int session_init(void);

// 获取会话（如果不存在则创建）
session_t* session_get_or_create(int netfd);

// 获取会话（不存在返回NULL）
session_t* session_get(int netfd);

// 更新会话路径
int session_update_path(int netfd, const char* new_path);

// 删除会话
int session_remove(int netfd);

// 销毁会话管理系统
int session_destroy(void);

#endif /* __SESSION_H__ */