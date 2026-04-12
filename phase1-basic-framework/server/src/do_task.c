#define _GNU_SOURCE
#include "../include/do_task.h"
#include "../include/network.h"
#include "../include/common.h"
#include "../include/session.h"
#include "../include/stack.h"
#include "../include/packet.h"
#include "../include/task_helper.h"
#include "../include/transmit_data.h"

int do_task(node_t * p_node){
    char session_path[PATH_MAX] = {0};
    
    // 从会话获取当前路径
    session_t* session = session_get(p_node->netfd);
    if(session != NULL && session->current_path[0] != '\0') {
        strcat(session_path, session->current_path);
    }
    
    switch(p_node->type) {
    case PROMPT:
        // 路径已经在add_task中更新到会话，这里不需要额外操作
        break;
    case PWD:
        pwd_dir(p_node->netfd, session_path);
        break;
    case CD:
        cd_dir(p_node->netfd, p_node->content, session_path);
        break;
    case LS:    
        ls_dir(p_node->netfd, p_node->content, session_path);
        break;
    case LL:
        ll_dir(p_node->netfd, p_node->content, session_path);
        break;
    case MKDIR:
        mk_dir(p_node->netfd, p_node->content, session_path);
        break;
    case RMDIR: 
        rm_dir(p_node->netfd, p_node->content, session_path);
        break;
    case RM:
        rm_file(p_node->netfd, p_node->content, session_path);
        break;
    case PUTS:
        puts_file_server(p_node->netfd, p_node->content, session_path);
        // 上传任务完成 添加监听
        add_epoll_readfd(p_node->epfd, p_node->netfd);
        break;
    case GETS:
        gets_file_server(p_node->netfd, p_node->content, session_path);
        break;
    case TREE:  
        tree_dir(p_node->netfd, p_node->content, session_path);
        break;
    case CAT:   
        cat_file(p_node->netfd, p_node->content, session_path);
        break;
    case NOTCMD:
        notcmd(p_node->netfd);
        printf("Invalid command!\n");
        break;
    default:
        break;
    }
    return 0;
}


int pwd_dir(int netfd, const char * session_path)
{
    packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = PWD;
    packet.status = 0;
    packet.length = strlen(session_path);
    memcpy(packet.content, session_path, packet.length);
    send_packet(netfd, &packet);
    return 0;
}

int cd_dir(int netfd, const char * cd_dir, const char * session_path) {
    int flag = 0; // 0 cd成功 -1 cd失败
    // 考虑复杂文件路径的问题
    // ../dir1/../dir2/./dir1/../dir1  --> /dir2/dir1
    char short_path[PATH_MAX] = {0};
    get_short_path(short_path, cd_dir, session_path);

    if(access(short_path, F_OK) != 0) {
        printf("cd 路径不存在\n");
        flag = -1;
    }
    else {
        // cd成功, 更新会话路径
        session_update_path(netfd, short_path);
    }

    packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = CD;
    packet.status = flag;

    if(flag == -1){
        strcpy(packet.content, "cd failed, path does not exist.\n");
        packet.length = strlen(packet.content);
    }
    else {
        strcpy(packet.content, short_path);
        packet.length = strlen(short_path);
    }
    send_packet(netfd, &packet);

    return 0;
}

int ls_dir(int netfd, const char * ls_dir, const char * session_path) {
    // 获取需要ls的目录或者文件的最短相对路径
    int flag = 0; // 0 ls成功 -1 ls失败
    char short_path[PATH_MAX] = {0};
    get_short_path(short_path, ls_dir, session_path);

    if(access(short_path, F_OK) != 0) {
        printf("目录或文件不存在\n");
        flag = -1;
    }

    packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = LS;
    packet.status = flag;

    struct stat st;
    memset(&st, 0, sizeof(st));
    if(flag == 0) stat(short_path, &st);

    if(flag == -1){
        strcpy(packet.content, "ls failed, directory or file does not exist.\n");
    }
    else {
        if (S_ISDIR(st.st_mode)) {
            // 目录
            DIR * dirp = opendir(short_path);
            if(dirp == NULL)
                return send_text_response(netfd, LS, -1, "ls failed, can not open directory.\n");
            struct dirent * pdirent = {0};
            while((pdirent = readdir(dirp))) {
                // 跳过.和..目录
                if(strcmp(pdirent->d_name, ".") == 0 || strcmp(pdirent->d_name, "..") == 0) 
                    continue;
                // 拼接文件名
                strcat(packet.content, pdirent->d_name);
                if(pdirent->d_type == DT_DIR) {
                    strcat(packet.content, "/"); // 如果是目录，后面加上/
                }
                strcat(packet.content, "  "); // 每项后面加上空格分隔
            }
            closedir(dirp); // 关闭目录
        }
        else {
            // 文件, 直接把文件名当成ls的结果返回
            // TODO: 使用short_path的最后一部分作为文件名返回给客户端, 目前直接返回ls_dir参数, 可能包含路径
            strcpy(packet.content, ls_dir);
        }
    }
    packet.length = strlen(packet.content);
    send_packet(netfd, &packet);
    return 0;
}

int ll_dir(int netfd, const char * ll_dir, const char * session_path) {
    int flag = 0;
    char short_path[PATH_MAX] = {0};
    get_short_path(short_path, ll_dir, session_path);

    if(access(short_path, F_OK) != 0) {
        printf("目录或文件不存在\n");
        flag = -1;
    }

    packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = LL;
    packet.status = flag;

    if(flag == -1) {
        strcpy(packet.content, "ll failed, directory or file does not exist.\n");
        packet.length = strlen(packet.content);
        send_packet(netfd, &packet);
        return 0;
    }

    struct stat st;
    memset(&st, 0, sizeof(st));
    if(stat(short_path, &st) == -1)
        return send_text_response(netfd, LL, -1, "ll failed, can not get file information.\n");

    if(S_ISDIR(st.st_mode)) {
        DIR *dirp = opendir(short_path);
        if(dirp == NULL)
            return send_text_response(netfd, LL, -1, "ll failed, can not open directory.\n");

        struct dirent *pdirent = NULL;
        while((pdirent = readdir(dirp)) != NULL) {
            if(strcmp(pdirent->d_name, ".") == 0 || strcmp(pdirent->d_name, "..") == 0)
                continue;

            char file_path[PATH_MAX] = {0};
            if(snprintf(file_path, sizeof(file_path), "%s/%s", short_path, pdirent->d_name) >= (int)sizeof(file_path)) {
                closedir(dirp);
                return send_text_response(netfd, LL, -1, "ll failed, file path is too long.\n");
            }

            int ret = append_ll_entry(packet.content, sizeof(packet.content), file_path, pdirent->d_name);
            if(ret == -1) {
                closedir(dirp);
                return send_text_response(netfd, LL, -1, "ll failed, can not get directory entry information.\n");
            }
            if(ret == 1) {
                closedir(dirp);
                return send_text_response(netfd, LL, -1, "ll failed, response content is too large.\n");
            }
        }
        closedir(dirp);
    }
    else {
        const char *display_name = strrchr(short_path, '/');
        display_name = (display_name == NULL) ? short_path : display_name + 1;
        int ret = append_ll_entry(packet.content, sizeof(packet.content), short_path, display_name);
        if(ret == -1)
            return send_text_response(netfd, LL, -1, "ll failed, can not get file information.\n");
        if(ret == 1)
            return send_text_response(netfd, LL, -1, "ll failed, response content is too large.\n");
    }

    packet.length = strlen(packet.content);
    send_packet(netfd, &packet);
    return 0;
}

int mk_dir(int netfd, const char * mk_dir, const char * session_path) {
    int flag = 0; // 0 ls成功 -1 ls失败
    char short_path[PATH_MAX] = {0};
    get_short_path(short_path, mk_dir, session_path);

    if(mkdir(short_path, 0755) < 0) {
        printf("创建目录失败\n");
        flag = -1;
    }

    packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = MKDIR;
    packet.status = flag;

    send_packet(netfd, &packet);
    return 0;
}

int rm_dir(int netfd, const char * rm_dir, const char * session_path) {
    int flag = 0; // 0 ls成功 -1 ls失败
    char short_path[PATH_MAX] = {0};
    get_short_path(short_path, rm_dir, session_path);

    if(rmdir(short_path) < 0) {
        printf("rmdir失败, 目录非空或不存在\n");
        flag = -1;
    }

    packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = RMDIR;
    packet.status = flag;
    send_packet(netfd, &packet);
    return 0;
}

int rm_file(int netfd, const char * rm_file, const char * session_path) {
    int flag = 0; // 0 ls成功 -1 ls失败
    char short_path[PATH_MAX] = {0};
    get_short_path(short_path, rm_file, session_path);

    if(unlink(short_path) < 0) {
        printf("unlink失败, 文件不存在\n");
        flag = -1;
    }

    packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = RM;
    packet.status = flag;
    send_packet(netfd, &packet);
    return 0;
}

int tree_dir(int netfd, const char * tree_dir, const char * session_path) 
{
    char short_path[PATH_MAX] = {0};
    get_short_path(short_path, tree_dir, session_path);

    struct stat st;
    memset(&st, 0, sizeof(st));
    if(stat(short_path, &st) == -1)
        return send_text_response(netfd, TREE, -1, "tree failed, directory or file does not exist.\n");

    packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = TREE;
    packet.status = 0;

    const char *display_name = strrchr(short_path, '/');
    display_name = (display_name == NULL) ? short_path : display_name + 1;
    packet.length = snprintf(packet.content,
                             sizeof(packet.content),
                             "%s%s\n",
                             display_name,
                             S_ISDIR(st.st_mode) ? "/" : "");
    if(packet.length < 0)
        packet.length = 0;
    if(packet.length > CONTENT_MAX)
        packet.length = CONTENT_MAX;

    if(send_packet(netfd, &packet) == -1)
        return -1;

    if(S_ISDIR(st.st_mode))
    {
        if(send_tree_entries(netfd, short_path, 1) == -1)
            return -1;
    }

    memset(&packet, 0, sizeof(packet));
    packet.type = TREE;
    packet.status = 0;
    packet.length = 0;
    return send_packet(netfd, &packet);
}

int cat_file(int netfd, const char * cat_file, const char * session_path)
{
    char short_path[PATH_MAX] = {0};
    get_short_path(short_path, cat_file, session_path);

    struct stat st;
    memset(&st, 0, sizeof(st));
    if(stat(short_path, &st) == -1)
        return send_text_response(netfd, CAT, -1, "cat failed, file does not exist.\n");

    if(S_ISDIR(st.st_mode))
        return send_text_response(netfd, CAT, -1, "cat failed, target is a directory.\n");

    int fd = open(short_path, O_RDONLY);
    if(fd == -1)
        return send_text_response(netfd, CAT, -1, "cat failed, can not open file.\n");

    packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = CAT;
    packet.status = 0;
    packet.length = sizeof(st.st_size);
    memcpy(packet.content, &st.st_size, sizeof(st.st_size));
    if(send_packet(netfd, &packet) == -1)
    {
        close(fd);
        return -1;
    }

    char buffer[CONTENT_MAX] = {0};
    while(1)
    {
        ssize_t read_bytes = read(fd, buffer, sizeof(buffer));
        if(read_bytes < 0)
        {
            close(fd);
            return -1;
        }
        if(read_bytes == 0)
            break;
        if(sendn(netfd, buffer, (int)read_bytes) == -1)
        {
            close(fd);
            return -1;
        }
    }

    close(fd);
    return 0;
}

int notcmd(int netfd) {
    packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = NOTCMD;
    packet.status = 0;
    send_packet(netfd, &packet);
    return 0;
}