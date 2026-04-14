#ifndef __SESSION_H__
#define __SESSION_H__

#include "packet.h"
#include "common.h"
#include <pthread.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <time.h>

#define MAX_SESSIONS 1024

typedef struct {
    int netfd;                              // 连接socket
    char current_path[PATH_MAX];            // 当前路径
    uid_t uid;                              // 用户ID
    gid_t gid;                              // 组ID
    char username[32];                      // 用户名
    int is_authenticated;                   // 认证状态
    char client_ip[INET_ADDRSTRLEN];        // 客户端IP
    int client_port;                        // 客户端端口
    time_t connect_time;                    // 连接建立时间
    time_t last_active_time;                // 最近活动时间
} session_t;

// 初始化会话管理系统
int session_init(void);

// 获取会话（如果不存在则创建）
session_t* session_get_or_create(int netfd);

// 获取会话（不存在返回NULL）
session_t* session_get(int netfd);

// 更新会话路径
int session_update_path(int netfd, const char* new_path);

// 更新连接信息
int session_set_connection_info(int netfd, const char *client_ip, int client_port, time_t connect_time);

// 更新最近活跃时间
int session_touch(int netfd, time_t active_time);

// 删除会话
int session_remove(int netfd);

// 销毁会话管理系统
int session_destroy(void);

#endif /* __SESSION_H__ */