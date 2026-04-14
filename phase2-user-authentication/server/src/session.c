#include "../include/session.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// 会话数组和锁
static session_t sessions[MAX_SESSIONS];
static int session_count = 0;
static pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;

// 初始化会话管理系统
int session_init(void) {
    pthread_mutex_lock(&session_mutex);
    session_count = 0;
    memset(sessions, 0, sizeof(sessions));
    pthread_mutex_unlock(&session_mutex);
    return 0;
}

// 获取会话（如果不存在则创建）
session_t* session_get_or_create(int netfd) {
    pthread_mutex_lock(&session_mutex);
    
    // 查找现有会话
    for (int i = 0; i < session_count; i++) {
        if (sessions[i].netfd == netfd) {
            pthread_mutex_unlock(&session_mutex);
            return &sessions[i];
        }
    }
    
    // 创建新会话
    if (session_count >= MAX_SESSIONS) {
        pthread_mutex_unlock(&session_mutex);
        fprintf(stderr, "Error: Maximum session count reached\n");
        return NULL;
    }
    
    sessions[session_count].netfd = netfd;
    strcpy(sessions[session_count].current_path, ""); // 初始为空路径
    strcpy(sessions[session_count].username, "anonymous");
    strcpy(sessions[session_count].client_ip, "unknown");
    sessions[session_count].client_port = -1;
    sessions[session_count].connect_time = 0;
    sessions[session_count].last_active_time = 0;
    session_t* new_session = &sessions[session_count];
    session_count++;
    
    pthread_mutex_unlock(&session_mutex);
    return new_session;
}

// 获取会话（不存在返回NULL）
session_t* session_get(int netfd) {
    pthread_mutex_lock(&session_mutex);
    
    for (int i = 0; i < session_count; i++) {
        if (sessions[i].netfd == netfd) {
            pthread_mutex_unlock(&session_mutex);
            return &sessions[i];
        }
    }
    
    pthread_mutex_unlock(&session_mutex);
    return NULL;
}

// 更新会话路径
int session_update_path(int netfd, const char* new_path) {
    session_t* session = session_get(netfd);
    if (session == NULL) {
        // 会话不存在，尝试创建
        session = session_get_or_create(netfd);
        if (session == NULL) {
            return -1;
        }
    }
    
    pthread_mutex_lock(&session_mutex);
    strncpy(session->current_path, new_path, PATH_MAX - 1);
    session->current_path[PATH_MAX - 1] = '\0'; // 确保终止
    pthread_mutex_unlock(&session_mutex);
    
    return 0;
}

int session_set_connection_info(int netfd, const char *client_ip, int client_port, time_t connect_time) {
    session_t* session = session_get_or_create(netfd);
    if (session == NULL) {
        return -1;
    }

    pthread_mutex_lock(&session_mutex);
    if(client_ip != NULL) {
        strncpy(session->client_ip, client_ip, sizeof(session->client_ip) - 1);
        session->client_ip[sizeof(session->client_ip) - 1] = '\0';
    }
    session->client_port = client_port;
    session->connect_time = connect_time;
    session->last_active_time = connect_time;
    pthread_mutex_unlock(&session_mutex);
    return 0;
}

int session_touch(int netfd, time_t active_time) {
    session_t* session = session_get(netfd);
    if (session == NULL) {
        return -1;
    }

    pthread_mutex_lock(&session_mutex);
    session->last_active_time = active_time;
    pthread_mutex_unlock(&session_mutex);
    return 0;
}

// 删除会话
int session_remove(int netfd) {
    pthread_mutex_lock(&session_mutex);
    
    for (int i = 0; i < session_count; i++) {
        if (sessions[i].netfd == netfd) {
            // 移动最后一个元素到当前位置
            sessions[i] = sessions[session_count - 1];
            session_count--;
            pthread_mutex_unlock(&session_mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&session_mutex);
    return -1; // 会话不存在
}

// 销毁会话管理系统
int session_destroy(void) {
    pthread_mutex_lock(&session_mutex);
    session_count = 0;
    memset(sessions, 0, sizeof(sessions));
    pthread_mutex_unlock(&session_mutex);
    return 0;
}