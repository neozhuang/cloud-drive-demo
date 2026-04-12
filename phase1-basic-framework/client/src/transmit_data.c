#include "../include/transmit_data.h"
#include "../include/common.h"
#include "../include/packet.h"
#include <sys/socket.h>

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

int gets_file_client(int sockfd, const packet_t *first_packet, const char* filename, const char* download_path)
{
    packet_t packet;
    off_t filesize = 0;
    off_t received = 0;
    const char *download_filename = filename;
    char file_path[PATH_MAX] = {0};

    if(first_packet->status != 0)
    {
        printf("%.*s\n\n", first_packet->length, first_packet->content);
        return -1;
    }

    if(first_packet->length < (int)sizeof(filesize)) return -1;
    memcpy(&filesize, first_packet->content, sizeof(filesize));

    if((download_filename = strrchr(filename, '/')) != NULL)
        ++download_filename;
    else
        download_filename = filename;

    snprintf(file_path, sizeof(file_path), "%s/%s", download_path, download_filename);

    int fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
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
            return -1;
        }

        if(packet.status != 0)
        {
            close(fd);
            printf("%.*s\n", packet.length, packet.content);
            return -1;
        }

        if(packet.length == 0)
            break;

        if(write(fd, packet.content, packet.length) != packet.length)
        {
            close(fd);
            return -1;
        }
        received += packet.length;
    }

    close(fd);
    if(received != filesize)
        return -1;

    printf("download file %s succeed.\n", download_filename);
    return 0;
}

int puts_file_client(int sockfd, const char* filename)
{
    packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = PUTS;
    // 可读打开上传文件
    int fd = open(filename, O_RDONLY);
    if(fd == -1)
    {
        packet.status = -1;
        packet.length = snprintf(packet.content, sizeof(packet.content), "Can not open file %s.", filename);
        if(packet.length < 0) packet.length = 0;
        send_packet(sockfd, &packet);
        printf("Can not open file %s.\n", filename);
        return -1;
    }

    //文件存在，获取上传文件大小
    struct stat statbuf;
    memset(&statbuf, 0, sizeof(statbuf));
    fstat(fd, &statbuf);
    off_t filesize = statbuf.st_size;

    // 发送文件大小作为首包
    packet.status = 0;
    packet.length = sizeof(filesize);
    memcpy(packet.content, &filesize, packet.length);
    send_packet(sockfd, &packet);
    printf("open file %s succeed.\n", filename);

    // 发送文件
    while(1)
    {
        memset(packet.content, 0, sizeof(packet.content));
        packet.length = read(fd, packet.content, sizeof(packet.content));
        if(packet.length > 0)
        {
            packet.status = 0;
            send_packet(sockfd, &packet);
        }
        else
            break;
    }
    // 发送结束标志
    packet.status = 0;
    packet.length = 0;
    send_packet(sockfd, &packet);
    close(fd);
    printf("send complete!\n");
    return 0;
}

int recv_stream_content(int sockfd)
{
    packet_t packet;
    while(1)
    {
        if(recv_packet(sockfd, &packet) == -1) return -1;
        printf("%.*s", packet.length, packet.content);
        if(packet.length == 0) break;
    }
    return 0;
}

int recv_cat_stream(int sockfd, const packet_t *first_packet)
{
    off_t filesize = 0;
    off_t remain = 0;
    char buffer[CONTENT_MAX] = {0};

    if(first_packet->status != 0)
    {
        printf("%.*s", first_packet->length, first_packet->content);
        if(first_packet->length > 0 && first_packet->content[first_packet->length - 1] != '\n')
            printf("\n");
        return -1;
    }

    if(first_packet->length < (int)sizeof(filesize))
        return -1;

    memcpy(&filesize, first_packet->content, sizeof(filesize));
    remain = filesize;
    while(remain > 0)
    {
        int chunk = remain > (off_t)sizeof(buffer) ? (int)sizeof(buffer) : (int)remain;
        int ret = recvn(sockfd, buffer, chunk);
        if(ret <= 0)
            return -1;
        fwrite(buffer, 1, ret, stdout);
        remain -= ret;
    }

    if(filesize > 0 && buffer[0] != '\0')
    {
        // no-op, just avoid empty body warning in some toolchains if needed later
    }
    return 0;
}