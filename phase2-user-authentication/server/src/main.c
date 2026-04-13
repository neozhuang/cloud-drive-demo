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
                int netfd = accept(listenfd, NULL, NULL);
                ERROR_CHECK(netfd, -1, "accept");
                printf("I am master, I got a netfd = %d\n", netfd);
                add_epoll_readfd(epfd, netfd);
            }
            else if(events[i].data.fd == exit_pipe[0]) {
                threadpool_stop(&threadpool);
                threadpool_destroy(&threadpool);
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
    if(recv_packet(netfd, &packet) == -1){
        // maybe client closed connection or error occurred, just close connection
        printf("\nconn %d is closed.\n", netfd);
        del_epoll_readfd(epfd, netfd);
        close(netfd);
        session_remove(netfd);  // 清理会话状态
        return 0;
    }

    // 特殊处理LOGIN命令（在主线程处理）
    if(packet.type == LOGIN) {
        // 1. 接收用户名
        char username[CONTENT_MAX];
        strncpy(username, packet.content, packet.length);
        username[packet.length] = '\0';
        
        // 2. 接收密码包
        if(recv_packet(netfd, &packet) == -1) {
            // 错误处理...
        }
        
        char password[CONTENT_MAX];
        strncpy(password, packet.content, packet.length);
        password[packet.length] = '\0';
        //printf("password: %s\n", password);
        
        // 3. 验证用户
        int auth_result = user_auth(username, password, netfd);
        
        // 4. 发送响应
        packet_t response;
        memset(&response, 0, sizeof(response));
        response.type = LOGIN;
        response.status = auth_result; // 0,1,2
        send_packet(netfd, &response);
        
        return 0; // 登录处理完成，不加入任务队列
    }

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

    if(packet.length > 0 && p_node->type == PUTS){
        // 上传任务 先暂时从epoll实例中删除监听
        del_epoll_readfd(epfd, netfd);                
    }
    queue_enque(queue, p_node);

    return 0;
}
