#include "../include/transmit_data.h"
#include "../include/common.h"
#include "../include/packet.h"
#include "../include/task_helper.h"

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
    // get short path of the file to be downloaded
    char short_path[PATH_MAX] = {0};
    get_short_path(short_path, command_content, session_path);

    struct stat statbuf;
    memset(&statbuf, 0, sizeof(statbuf));
    if(stat(short_path, &statbuf) == -1)
    {
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
        return send_text_response(sockfd, GETS, -1, "gets failed, target is a directory.\n");

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
        return send_text_response(sockfd, GETS, -1, "gets failed, can not open file.\n");

    while(1)
    {
        memset(packet.content, 0, sizeof(packet.content));
        packet.length = read(fd, packet.content, sizeof(packet.content));
        if(packet.length < 0)
        {
            close(fd);
            return -1;
        }
        if(packet.length == 0)
            break;
        packet.status = 0;
        if(send_packet(sockfd, &packet) == -1)
        {
            close(fd);
            return -1;
        }
    }

    close(fd);
    packet.length = 0;
    packet.status = 0;
    return send_packet(sockfd, &packet);
}

int puts_file_server(int sockfd, const char * command_content, const char * session_path)
{
    packet_t packet;
    off_t filesize = 0;
    off_t received = 0;
    const char *upload_filename = command_content;
    char file_path[PATH_MAX] = {0};

    memset(&packet, 0, sizeof(packet));
    if(recv_packet(sockfd, &packet) == -1)
        return -1;

    if(packet.status != 0)
    {
        printf("%.*s\n", packet.length, packet.content);
        return -1;
    }

    if(packet.length < (int)sizeof(filesize))
        return -1;
    memcpy(&filesize, packet.content, sizeof(filesize));

    if((upload_filename = strrchr(command_content, '/')) != NULL)
        ++upload_filename;
    else
        upload_filename = command_content;

    if(snprintf(file_path, sizeof(file_path), "%s/%s", session_path, upload_filename) >= (int)sizeof(file_path))
        return send_text_response(sockfd, PUTS, -1, "puts failed, target path is too long.\n");

    int fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if(fd == -1)
        return send_text_response(sockfd, PUTS, -1, "puts failed, can not create file.\n");

    while(1)
    {
        memset(&packet, 0, sizeof(packet));
        if(recv_packet(sockfd, &packet) == -1)
        {
            close(fd);
            return -1;
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
            return send_text_response(sockfd, PUTS, -1, "puts failed, can not write file.\n");
        }
        received += packet.length;
    }

    close(fd);
    if(received != filesize)
        return send_text_response(sockfd, PUTS, -1, "puts failed, file size mismatch.\n");

    return send_text_response(sockfd, PUTS, 0, "Upload file successfully!\n");
}
