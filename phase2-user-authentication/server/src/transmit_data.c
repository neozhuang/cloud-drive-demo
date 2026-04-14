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
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);
    packet_t packet;
    off_t filesize = 0;
    off_t received = 0;
    const char *upload_filename = command_content;
    char file_path[PATH_MAX] = {0};
    session_t *session = session_get(sockfd);

    memset(&packet, 0, sizeof(packet));
    if(recv_packet(sockfd, &packet) == -1)
    {
        log_transfer_event(sockfd,
                           session != NULL ? session->username : "anonymous",
                           session != NULL ? session->client_ip : "unknown",
                           session != NULL ? session->client_port : -1,
                           "upload",
                           upload_filename,
                           0,
                           -1,
                           0,
                           "receive file header failed");
        return -1;
    }

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
                           "client send file header error");
        return -1;
    }

    if(packet.length < (int)sizeof(filesize))
    {
        log_transfer_event(sockfd,
                           session != NULL ? session->username : "anonymous",
                           session != NULL ? session->client_ip : "unknown",
                           session != NULL ? session->client_port : -1,
                           "upload",
                           upload_filename,
                           0,
                           -1,
                           0,
                           "invalid file size header");
        return -1;
    }
    memcpy(&filesize, packet.content, sizeof(filesize));

    if((upload_filename = strrchr(command_content, '/')) != NULL)
        ++upload_filename;
    else
        upload_filename = command_content;

    if(snprintf(file_path, sizeof(file_path), "%s/%s", session_path, upload_filename) >= (int)sizeof(file_path))
        return send_text_response(sockfd, PUTS, -1, "puts failed, target path is too long.\n");

    int fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if(fd == -1)
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
                           "can not create file");
        return send_text_response(sockfd, PUTS, -1, "puts failed, can not create file.\n");
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

        if(packet.status != 0)
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
                               "client send file content error");
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
    if(received != filesize)
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
