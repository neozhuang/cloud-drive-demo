/*
 * % ============================================================================
 * Author       : neozhuang
 * Email        : zhuangzhuangzhang97@gmail.com
 * CreateTime   : 2026-04-01 19:48:28 +0800
 * LastEditTime : 2026-04-02 15:02:29 +0800
 * Github       : https://github.com/neozhuang/
 * FilePath     : cloud-drive-demo/phase1-basic-framework/server/src/main.c
 * Description  : server main
 * % ============================================================================
 */

#include "../include/packet.h"
#include "../include/session.h"
#include "../include/config.h"
#include "../include/threadpool.h"
#include "../include/network.h"
#include "../include/do_task.h"
#include "../include/transmit_data.h"
#include "../include/user_auth.h"
#include "../include/log.h"

#define EVENTS_MAX 1024
#define CLOUD_DRIVE_DIR "cloud-drive"

int add_task(block_queue_t* queue, int epfd, int netfd);
int exit_pipe[2];

void sig_handler(int num) {
    printf("Parent got a signal, signum = %d\n", num);
    write(exit_pipe[1], "1", 1);
}

// ./server
// ./server [*.conf]
// ./server [server_ip] [server_port] [thread_num]
int main(int argc, char * argv[])
{
    config_t config;
    memset(&config, 0, sizeof(config));
    // read config info
    if(read_config(argc, argv, &config) < 0) {
        printf("read config failed!\n");
        return -1;
    }

    if(log_init(config.log_path) < 0) {
        perror("log_init");
        return -1;
    }
    log_write(LOG_LEVEL_INFO, "SERVER", "server starting ip=%s port=%d thread_count=%d log_path=\"%s\"",
              config.server_ip,
              config.port,
              config.thread_count,
              config.log_path);

    pid_t pid;
    int listenfd;
    const char cloud_drive_dir[NAME_MAX] = CLOUD_DRIVE_DIR;
    mkdir(cloud_drive_dir, 0777);

    // create pipe
    pipe(exit_pipe);

    if((pid = fork()) < 0){
        perror("fork error");
        exit(-1);
    }
    else if(pid > 0){
        close(exit_pipe[0]);                // parent: close read end
        signal(SIGUSR1, sig_handler);
        waitpid(pid, NULL, 0);
        printf("\nparent process exit.\n");
        log_close();
        exit(0);
    }

    // child
    close(exit_pipe[1]);        // child: close write end

    chdir(cloud_drive_dir); // 切换到云盘根目录

    // 初始化会话管理系统
    if(session_init() < 0){
        printf("session_init error\n");
        exit(-2);
    }

    threadpool_t threadpool;
    memset(&threadpool, 0, sizeof(threadpool));
    if(threadpool_init(&threadpool, config.thread_count) < 0){
        printf("threadpool_init error\n");
        exit(-3);
    }
    if(threadpool_start(&threadpool) < 0){
        printf("threadpool_start error\n");
        exit(-4);
    }

    // create listen socket: socket setsockopt bind listen
    if((listenfd = tcp_init(config.server_ip, config.port)) < 0){
        perror("tcp_init");
        exit(-1);
    }
    printf("listenfd = %d\n", listenfd);

    // create epoll instance
    int epfd = epoll_create1(0);

    add_epoll_readfd(epfd, listenfd);
    add_epoll_readfd(epfd, exit_pipe[0]);

    struct epoll_event events[EVENTS_MAX];
    while(1) {
        int nfds = epoll_wait(epfd, events, EVENTS_MAX, -1);
        if(nfds == -1){
            if(errno == EINTR) {
                printf("EINTR: interupted by signal\n");
                continue;
            }else{
                perror("epoll_wait error");
                break;
            }
        }
        for(int i = 0; i < nfds; ++i) {
            if(events[i].data.fd == listenfd) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                memset(&client_addr, 0, sizeof(client_addr));
                int netfd = accept(listenfd, (struct sockaddr *)&client_addr, &client_len);
                ERROR_CHECK(netfd, -1, "accept");
                char client_ip[INET_ADDRSTRLEN] = "unknown";
                inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
                int client_port = ntohs(client_addr.sin_port);
                session_set_connection_info(netfd, client_ip, client_port, time(NULL));
                log_connection_event("CONNECT", netfd, client_ip, client_port, "anonymous", "client connected");
                add_epoll_readfd(epfd, netfd);
                printf("\nnew connection: netfd=%d client=%s:%d\n", netfd, client_ip, client_port);
            }
            else if(events[i].data.fd == exit_pipe[0]) {
                log_write(LOG_LEVEL_INFO, "SERVER", "server shutting down");
                threadpool_stop(&threadpool);
                threadpool_destroy(&threadpool);
                session_destroy();
                log_close();
                printf("Master is going to exit.\n");
                exit(0);
            }
            else {
                add_task(&threadpool.queue, epfd, events[i].data.fd);
            }
        }
    }
    return 0;
}

int add_task(block_queue_t* queue, int epfd, int netfd)
{    
    packet_t packet;
    session_t *session = session_get_or_create(netfd);
    if(recv_packet(netfd, &packet) == -1){
        // maybe client closed connection or error occurred, just close connection
        if(session != NULL) {
            long online_seconds = 0;
            if(session->connect_time != 0) {
                online_seconds = (long)(time(NULL) - session->connect_time);
            }
            log_connection_event("DISCONNECT",
                                 netfd,
                                 session->client_ip,
                                 session->client_port,
                                 session->username,
                                 online_seconds > 0 ? "client closed connection" : "client disconnected");
            if(online_seconds > 0) {
                log_write(LOG_LEVEL_INFO,
                          "SESSION",
                          "netfd=%d user=%s client=%s:%d online_seconds=%ld",
                          netfd,
                          session->username[0] != '\0' ? session->username : "anonymous",
                          session->client_ip,
                          session->client_port,
                          online_seconds);
            }
        }
        del_epoll_readfd(epfd, netfd);
        session_remove(netfd);  // 清理会话状态
        close(netfd);
        printf("\nconn %d is closed.\n", netfd);
        return 0;
    }

    session_touch(netfd, time(NULL));

    // 特殊处理LOGIN命令（在主线程处理）
    if(packet.type == LOGIN) {
        // 1. 接收用户名
        char username[CONTENT_MAX];
        strncpy(username, packet.content, packet.length);
        username[packet.length] = '\0';
        log_request_event(netfd,
                          session != NULL ? session->username : "anonymous",
                          session != NULL ? session->client_ip : "unknown",
                          session != NULL ? session->client_port : -1,
                          LOGIN,
                          username);
        
        // 2. 接收密码包
        if(recv_packet(netfd, &packet) == -1) {
            log_write(LOG_LEVEL_WARN,
                      "AUTH",
                      "netfd=%d user=%s client=%s:%d detail=\"password packet receive failed\"",
                      netfd,
                      username,
                      session != NULL ? session->client_ip : "unknown",
                      session != NULL ? session->client_port : -1);
            return -1;
        }
        
        char password[CONTENT_MAX];
        strncpy(password, packet.content, packet.length);
        password[packet.length] = '\0';
        
        // 3. 验证用户
        int auth_result = user_auth(username, password, netfd);
        session = session_get(netfd);
        log_write(auth_result == 0 ? LOG_LEVEL_INFO : LOG_LEVEL_WARN,
                  "AUTH",
                  "netfd=%d client=%s:%d user=%s status=%d detail=\"%s\"",
                  netfd,
                  session != NULL ? session->client_ip : "unknown",
                  session != NULL ? session->client_port : -1,
                  username,
                  auth_result,
                  auth_result == 0 ? "login success" : (auth_result == 1 ? "user not exist" : (auth_result == 2 ? "password incorrect" : "login failed")));
        
        // 4. 发送响应
        packet_t response;
        memset(&response, 0, sizeof(response));
        response.type = LOGIN;
        response.status = auth_result; // 0,1,2
        send_packet(netfd, &response);
        
        return 0; // 登录处理完成，不加入任务队列
    }

    log_request_event(netfd,
                      session != NULL ? session->username : "anonymous",
                      session != NULL ? session->client_ip : "unknown",
                      session != NULL ? session->client_port : -1,
                      packet.type,
                      packet.length > 0 ? packet.content : "");

    node_t * p_node = (node_t*)calloc(1, sizeof(node_t));
    p_node->netfd = netfd;
    p_node->epfd = epfd;
    p_node->type = packet.type;

    // 复制命令内容
    if(packet.length > 0) {
        memcpy(p_node->content, packet.content, packet.length);
        p_node->content[packet.length] = '\0';  // 确保字符串终止
    } else {
        p_node->content[0] = '\0';
    }

    if(packet.length > 0 && (p_node->type == PUTS || p_node->type == GETS)){
        // 文件传输任务先暂时从 epoll 实例中删除监听，避免主线程与工作线程同时读取同一 socket
        del_epoll_readfd(epfd, netfd);
    }
    queue_enque(queue, p_node);

    return 0;
}
