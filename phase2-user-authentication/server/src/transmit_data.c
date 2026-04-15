#include "../include/transmit_data.h"
#include "../include/common.h"
#include "../include/packet.h"
#include "../include/task_helper.h"
#include "../include/session.h"
#include "../include/log.h"

static int packet_bytes(const packet_t *packet)
{
    return (int)(sizeof(packet->type) + sizeof(packet->status) + sizeof(packet->length) + packet->length);
}

// 发送消息
int sendn(int sockfd, const void * buff, int len)
{
    char * p = (char *)buff;
    int cur_pos = 0;
    while (cur_pos < len) {
        int ret = send(sockfd, p + cur_pos, len - cur_pos, 0);
        if(ret == -1){
            return -1;
        }
        cur_pos += ret;
    }
    return cur_pos;
}

int recvn(int sockfd, void * buff, int len)
{
    char * p = (char *)buff;
    int cur_pos = 0;
    while (cur_pos < len) {
        int ret = recv(sockfd, p + cur_pos, len - cur_pos, 0);
        if(ret == -1){
            return -1;
        }
        else if(ret == 0){
            break;
        }
        cur_pos += ret;
    }
    return cur_pos;
}

int send_packet(int sockfd, const packet_t *packet)
{
    return sendn(sockfd, packet, packet_bytes(packet));
}


int recv_packet(int sockfd, packet_t *packet)
{
    memset(packet, 0, sizeof(*packet));
    if(recvn(sockfd, &packet->type, sizeof(packet->type)) <= 0) return -1;
    if(recvn(sockfd, &packet->status, sizeof(packet->status)) <= 0) return -1;
    if(recvn(sockfd, &packet->length, sizeof(packet->length)) <= 0) return -1;
    if(packet->length < 0 || packet->length > CONTENT_MAX) return -1;
    if(packet->length > 0 && recvn(sockfd, packet->content, packet->length) <= 0) return -1;
    return 0;
}

int gets_file_server(int sockfd, const char * command_content, const char * session_path)
{
    // 添加断点续传功能
    // 当前版本练习使用mmap进行文件传输优化，后续版本可以考虑使用sendfile系统调用进一步优化性能

    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);
    // get short path of the file to be downloaded
    char short_path[PATH_MAX] = {0};
    get_short_path(short_path, command_content, session_path);
    const char *filename = command_content;
    session_t *session = session_get(sockfd);

    struct stat statbuf;
    memset(&statbuf, 0, sizeof(statbuf));
    if(stat(short_path, &statbuf) == -1)
    {
        log_transfer_event(sockfd,
                           session != NULL ? session->username : "anonymous",
                           session != NULL ? session->client_ip : "unknown",
                           session != NULL ? session->client_port : -1,
                           "download",
                           filename,
                           0,
                           -1,
                           0,
                           "file does not exist");
        packet_t packet;
        memset(&packet, 0, sizeof(packet));
        packet.type = GETS;
        packet.status = -1;
        packet.length = snprintf(packet.content,
                                 sizeof(packet.content),
                                 "gets failed, file does not exist.\n");
        if(packet.length < 0) packet.length = 0;
        if(packet.length > CONTENT_MAX) packet.length = CONTENT_MAX;
        return send_packet(sockfd, &packet);
    }

    if(S_ISDIR(statbuf.st_mode))
    {
        log_transfer_event(sockfd,
                           session != NULL ? session->username : "anonymous",
                           session != NULL ? session->client_ip : "unknown",
                           session != NULL ? session->client_port : -1,
                           "download",
                           filename,
                           0,
                           -1,
                           0,
                           "target is a directory");
        return send_text_response(sockfd, GETS, -1, "gets failed, target is a directory.\n");
    }

    packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = GETS;
    packet.status = 0;
    packet.length = sizeof(statbuf.st_size);
    memcpy(packet.content, &statbuf.st_size, sizeof(statbuf.st_size));
    if(send_packet(sockfd, &packet) == -1)
        return -1;

    // 等待客户端发送断点续传信息
    memset(&packet, 0, sizeof(packet));
    if(recv_packet(sockfd, &packet) == -1)
        return -1;

    off_t offset = 0;
    if(packet.length == (int)sizeof(offset))
        memcpy(&offset, packet.content, sizeof(offset));

    // 客户端已经有文件了
    if(offset == statbuf.st_size) return 0;

    int fd = open(short_path, O_RDONLY);
    if(fd == -1)
    {
        log_transfer_event(sockfd,
                           session != NULL ? session->username : "anonymous",
                           session != NULL ? session->client_ip : "unknown",
                           session != NULL ? session->client_port : -1,
                           "download",
                           filename,
                           statbuf.st_size,
                           -1,
                           0,
                           "can not open file");
        return send_text_response(sockfd, GETS, -1, "gets failed, can not open file.\n");
    }

    // 跳过已下载部分
    if(offset > 0)
    {
        if(lseek(fd, offset, SEEK_SET) == -1)
        {
            close(fd);
            log_transfer_event(sockfd,
                               session != NULL ? session->username : "anonymous",
                               session != NULL ? session->client_ip : "unknown",
                               session != NULL ? session->client_port : -1,
                               "download",
                               filename,
                               statbuf.st_size,
                               -1,
                               0,
                               "lseek failed");
            return send_text_response(sockfd, GETS, -1, "gets failed, server internal error.\n");
        }
    }

    while(1)
    {
        memset(packet.content, 0, sizeof(packet.content));
        packet.length = read(fd, packet.content, sizeof(packet.content));
        if(packet.length < 0)
        {
            close(fd);
            log_transfer_event(sockfd,
                               session != NULL ? session->username : "anonymous",
                               session != NULL ? session->client_ip : "unknown",
                               session != NULL ? session->client_port : -1,
                               "download",
                               filename,
                               statbuf.st_size,
                               -1,
                               0,
                               "file read error");
            return -1;
        }
        if(packet.length == 0)
            break;
        packet.status = 0;
        if(send_packet(sockfd, &packet) == -1)
        {
            close(fd);
            log_transfer_event(sockfd,
                               session != NULL ? session->username : "anonymous",
                               session != NULL ? session->client_ip : "unknown",
                               session != NULL ? session->client_port : -1,
                               "download",
                               filename,
                               statbuf.st_size,
                               -1,
                               0,
                               "send packet failed");
            return -1;
        }
    }

    close(fd);
    packet.length = 0;
    packet.status = 0;
    if(send_packet(sockfd, &packet) == -1)
        return -1;

    gettimeofday(&end_time, NULL);
    long duration_ms = (end_time.tv_sec - start_time.tv_sec) * 1000L
                     + (end_time.tv_usec - start_time.tv_usec) / 1000L;
    log_transfer_event(sockfd,
                       session != NULL ? session->username : "anonymous",
                       session != NULL ? session->client_ip : "unknown",
                       session != NULL ? session->client_port : -1,
                       "download",
                       filename,
                       statbuf.st_size,
                       0,
                       duration_ms,
                       "download success");
    return 0;
}

int puts_file_server(int sockfd, const char * command_content, const char * session_path)
{
    // 添加断点续传功能
    // 当前版本练习使用mmap进行文件传输优化，后续版本可以考虑使用sendfile系统调用进一步优化性能

    // 1. 接收到的command_content是要上传的文件名
    // 2. 服务端发送这个文件的大小，如果不存在大小设置为0
    // 3. 客户端第一个包收的就是这个服务端的文件大小
    // 4. 客户端发送文件大小，如果不存在，status = -1 return
    // 5. 服务端接收客户端发来的文件大小,如果status = -1 return
    // 6. 客户端打开文件，lseek到对应的偏移，开始发送
    // 7. 服务端根据local_size来追加打开或者截断打开，开始接收

    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);
    off_t received = 0;
    off_t local_filesize = 0;
    const char *upload_filename = command_content;
    char file_path[PATH_MAX] = {0};
    session_t *session = session_get(sockfd);
    packet_t packet;
    off_t filesize = 0;
    struct stat statbuf;

    if((upload_filename = strrchr(command_content, '/')) != NULL)
        ++upload_filename;
    else
        upload_filename = command_content;

    // 2. 服务端发送断点续传偏移值给客户端
    if(snprintf(file_path, sizeof(file_path), "%s/%s", session_path, upload_filename) >= (int)sizeof(file_path))
        return send_text_response(sockfd, PUTS, -1, "puts failed, target path is too long.\n");
    memset(&statbuf, 0, sizeof(statbuf));
    if(stat(file_path, &statbuf) == 0)
        local_filesize = statbuf.st_size;
    else
        local_filesize = 0;

    // send resume_packet
    packet_t resume_packet;
    memset(&resume_packet, 0, sizeof(resume_packet));
    resume_packet.type = PUTS;
    resume_packet.status = 0;
    resume_packet.length = sizeof(local_filesize);
    memcpy(resume_packet.content, &local_filesize, sizeof(local_filesize));
    if(send_packet(sockfd, &resume_packet) == -1)
        return -1;
    
    // 5. 服务端接收客户端发来的文件大小,如果status = -1 return
    memset(&packet, 0, sizeof(packet));
    if(recv_packet(sockfd, &packet) == -1)
        return -1;

    if(packet.type != PUTS)
        return send_text_response(sockfd, PUTS, -1, "puts failed, invalid upload header.\n");

    if(packet.status != 0)
    {
        printf("%.*s\n", packet.length, packet.content);
        log_transfer_event(sockfd,
                           session != NULL ? session->username : "anonymous",
                           session != NULL ? session->client_ip : "unknown",
                           session != NULL ? session->client_port : -1,
                           "upload",
                           upload_filename,
                           0,
                           -1,
                           0,
                           "uploaded file is not exist");
        return -1;
    }

    if(packet.length != (int)sizeof(filesize))
        return send_text_response(sockfd, PUTS, -1, "puts failed, invalid file size header.\n");

    memcpy(&filesize, packet.content, sizeof(filesize));

    if(local_filesize > filesize)
        local_filesize = 0;

    if(local_filesize == filesize)
        return send_text_response(sockfd, PUTS, 0, "Upload file successfully!\n");

    // 7. 服务端根据local_size来追加打开或者截断打开，开始接收
    int open_flags = O_WRONLY | O_CREAT;
    if(local_filesize > 0)
        open_flags |= O_APPEND;
    else
        open_flags |= O_TRUNC;

    int fd = open(file_path, open_flags, 0666);
    if(fd == -1)
    {
        perror("open");
        return -1;
    }

    while(1)
    {
        memset(&packet, 0, sizeof(packet));
        if(recv_packet(sockfd, &packet) == -1)
        {
            close(fd);
            log_transfer_event(sockfd,
                               session != NULL ? session->username : "anonymous",
                               session != NULL ? session->client_ip : "unknown",
                               session != NULL ? session->client_port : -1,
                               "upload",
                               upload_filename,
                               filesize,
                               -1,
                               0,
                               "receive file content failed");
            return -1;
        }

        if(packet.type != PUTS)
        {
            close(fd);
            return send_text_response(sockfd, PUTS, -1, "puts failed, invalid upload packet.\n");
        }

        if(packet.status != 0)
        {
            close(fd);
            return send_text_response(sockfd, PUTS, -1, "puts failed, client send file error.\n");
        }

        if(packet.length == 0)
            break;

        if(write(fd, packet.content, packet.length) != packet.length)
        {
            close(fd);
            log_transfer_event(sockfd,
                               session != NULL ? session->username : "anonymous",
                               session != NULL ? session->client_ip : "unknown",
                               session != NULL ? session->client_port : -1,
                               "upload",
                               upload_filename,
                               filesize,
                               -1,
                               0,
                               "write file failed");
            return send_text_response(sockfd, PUTS, -1, "puts failed, can not write file.\n");
        }
        received += packet.length;
    }

    close(fd);
    if(received != filesize - local_filesize)
    {
        log_transfer_event(sockfd,
                           session != NULL ? session->username : "anonymous",
                           session != NULL ? session->client_ip : "unknown",
                           session != NULL ? session->client_port : -1,
                           "upload",
                           upload_filename,
                           filesize,
                           -1,
                           0,
                           "file size mismatch");
        return send_text_response(sockfd, PUTS, -1, "puts failed, file size mismatch.\n");
    }

    gettimeofday(&end_time, NULL);
    long duration_ms = (end_time.tv_sec - start_time.tv_sec) * 1000L
                     + (end_time.tv_usec - start_time.tv_usec) / 1000L;
    log_transfer_event(sockfd,
                       session != NULL ? session->username : "anonymous",
                       session != NULL ? session->client_ip : "unknown",
                       session != NULL ? session->client_port : -1,
                       "upload",
                       upload_filename,
                       filesize,
                       0,
                       duration_ms,
                       "upload success");
    return send_text_response(sockfd, PUTS, 0, "Upload file successfully!\n");
}
