#include "../include/transmit_data.h"
#include "../include/common.h"
#include "../include/packet.h"
#include <sys/socket.h>
#include <sys/mman.h>

#define MMAP_THRESHOLD (100 * 1024 * 1024)  // 100MB

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
    packet_t resume_packet;
    off_t filesize = 0;
    off_t received = 0;
    off_t local_filesize = 0;
    const char *download_filename = filename;
    char file_path[PATH_MAX] = {0};
    struct stat statbuf;

    if(first_packet->status != 0)
    {
        printf("%.*s\n\n", first_packet->length, first_packet->content);
        return -1;
    }

    // 文件存在，获取文件大小
    if(first_packet->length < (int)sizeof(filesize)) return -1;
    memcpy(&filesize, first_packet->content, sizeof(filesize));

    if((download_filename = strrchr(filename, '/')) != NULL)
        ++download_filename;
    else
        download_filename = filename;

    if(snprintf(file_path, sizeof(file_path), "%s/%s", download_path, download_filename) >= (int)sizeof(file_path))
    {
        printf("download path is too long.\n");
        return -1;
    }

    // 添加断点续传功能，首先检查本地是否已有部分文件，如果有则告知服务器从何处开始传输
    memset(&statbuf, 0, sizeof(statbuf));
    if(stat(file_path, &statbuf) == 0)
    {
        local_filesize = statbuf.st_size;
    }
    else
    {
        local_filesize = 0;
    }

    // 本地文件比服务端还大，说明本地文件异常，重新下载
    if(local_filesize > filesize)
    {
        local_filesize = 0;
    }

    memset(&resume_packet, 0, sizeof(resume_packet));
    resume_packet.type = GETS;
    resume_packet.status = 0;
    resume_packet.length = sizeof(local_filesize);
    memcpy(resume_packet.content, &local_filesize, sizeof(local_filesize));
    if(send_packet(sockfd, &resume_packet) == -1)
        return -1;

    if(local_filesize == filesize)
    {
        printf("File already fully downloaded.\n");
        return 0;
    }

    if(local_filesize > 0)
        printf("Resuming download from byte %ld...\n", local_filesize);
    else
        printf("Starting download...\n");

    // 计算剩余需要接收的数据量
    off_t remaining = filesize - local_filesize;
    
    // 大文件优化：超过100MB使用mmap接收
    if(remaining >= MMAP_THRESHOLD)
    {
        printf("Large file detected for download (%ld bytes >= %d), using mmap optimization for receiving...\n", 
               remaining, MMAP_THRESHOLD);
        
        // 打开文件，支持断点续传
        int open_flags = O_RDWR | O_CREAT;
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
        
        // 如果本地文件大小为0，需要预先设置文件大小
        if(local_filesize == 0)
        {
            if(ftruncate(fd, filesize) == -1)
            {
                perror("ftruncate");
                close(fd);
                printf("Can not set file size.\n");
                return -1;
            }
        }
        
        // 映射文件剩余部分到内存
        void *mapped = mmap(NULL, remaining, PROT_WRITE, MAP_SHARED, fd, local_filesize);
        if(mapped == MAP_FAILED)
        {
            perror("mmap failed, falling back to traditional method");
            close(fd);
            // 回退到传统方式
            goto traditional_method;
        }
        
        printf("File mapped at address %p, size %ld bytes for receiving\n", mapped, remaining);
        
        char *write_ptr = (char *)mapped;
        off_t bytes_written = 0;
        
        while(1)
        {
            memset(&packet, 0, sizeof(packet));
            if(recv_packet(sockfd, &packet) == -1)
            {
                munmap(mapped, remaining);
                close(fd);
                printf("Receive packet failed.\n");
                return -1;
            }

            if(packet.status != 0)
            {
                munmap(mapped, remaining);
                close(fd);
                printf("%.*s\n", packet.length, packet.content);
                return -1;
            }

            if(packet.length == 0)
                break;

            // 直接将数据复制到映射的内存区域
            memcpy(write_ptr + bytes_written, packet.content, packet.length);
            bytes_written += packet.length;
            received += packet.length;
            
            // 每接收10MB打印一次进度
            if(bytes_written % (10 * 1024 * 1024) < packet.length || bytes_written == remaining)
            {
                printf("Download progress: %ld/%ld bytes (%.1f%%)\n", 
                       bytes_written, remaining, (double)bytes_written * 100.0 / remaining);
            }
        }
        
        // 确保数据写入磁盘
        if(msync(mapped, remaining, MS_SYNC) == -1)
        {
            perror("msync");
            // 继续执行，不是致命错误
        }
        
        // 解除映射
        if(munmap(mapped, remaining) == -1)
        {
            perror("munmap");
            close(fd);
            return -1;
        }
        
        close(fd);
        
        if(received != remaining)
        {
            printf("File size mismatch: received %ld, expected %ld\n", received, remaining);
            return -1;
        }
        
        printf("mmap download completed, received %ld bytes\n", received);
    }
    else
    {
        // 传统方式接收小文件
        traditional_method:
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
        if(received != remaining)
            return -1;
    }

    printf("download file %s succeed.\n", download_filename);
    return 0;
}

int puts_file_client(int sockfd, const packet_t *first_packet, const char* filename)
{

    //大文件传输优化
    //mmap 将大文件映射入内存，进行网络传递。当发现文件大于 100M，就使用 mmap 方式映射该文件，然后再传输。
    off_t offset = 0;
    off_t filesize = 0;
    struct stat statbuf;
    packet_t packet;
    packet_t final_packet;

    if(first_packet->status != 0)
    {
        printf("%.*s", first_packet->length, first_packet->content);
        if(first_packet->length > 0 && first_packet->content[first_packet->length - 1] != '\n')
            printf("\n");
        return -1;
    }

    if(first_packet->length != (int)sizeof(offset))
        return -1;
    memcpy(&offset, first_packet->content, sizeof(offset));

    memset(&packet, 0, sizeof(packet));
    packet.type = PUTS;
    memset(&statbuf, 0, sizeof(statbuf));
    if(stat(filename, &statbuf) == 0)
    {
        filesize = statbuf.st_size;
        packet.status = 0;
        packet.length = sizeof(filesize);
        memcpy(packet.content, &filesize, packet.length);
        if(send_packet(sockfd, &packet) == -1)
            return -1;
    }
    else
    {
        packet.status = -1;
        packet.length = snprintf(packet.content, sizeof(packet.content), "file %s is not exist.\n", filename);
        printf("file %s is not exist.\n", filename);
        if(packet.length < 0) packet.length = 0;
        if(packet.length > CONTENT_MAX) packet.length = CONTENT_MAX;
        send_packet(sockfd, &packet);
        return -1;
    }

    if(offset < 0)
        offset = 0;
    if(offset > filesize)
        offset = 0;

    if(offset == filesize)
    {
        printf("File already fully uploaded.\n");
        if(recv_packet(sockfd, &final_packet) == -1)
            return -1;
        printf("%.*s", final_packet.length, final_packet.content);
        if(final_packet.length > 0 && final_packet.content[final_packet.length - 1] != '\n')
            printf("\n");
        return final_packet.status == 0 ? 0 : -1;
    }

    int fd = open(filename, O_RDONLY);
    if(fd == -1)
        return -1;

    if(offset > 0)
    {
        if(lseek(fd, offset, SEEK_SET) == -1)
        {
            close(fd);
            return -1;
        }
        printf("Resuming upload from byte %ld...\n", offset);
    }
    else
    {
        printf("Starting upload...\n");
    }

    // 大文件传输优化：超过100MB使用mmap
    if(filesize >= MMAP_THRESHOLD)
    {
        printf("Large file detected (%ld bytes >= %d), using mmap optimization...\n", filesize, MMAP_THRESHOLD);
        
        off_t remaining = filesize - offset;
        off_t current_pos = offset;
        
        // 映射整个文件（从偏移开始）
        void *mapped = mmap(NULL, remaining, PROT_READ, MAP_PRIVATE, fd, offset);
        if(mapped == MAP_FAILED)
        {
            perror("mmap failed");
            close(fd);
            return -1;
        }
        
        printf("File mapped at address %p, size %ld bytes\n", mapped, remaining);
        
        char *data_ptr = (char *)mapped;
        off_t mapped_offset = 0;
        
        while(mapped_offset < remaining)
        {
            int chunk_size = (remaining - mapped_offset) > CONTENT_MAX ? CONTENT_MAX : (int)(remaining - mapped_offset);
            
            memset(packet.content, 0, sizeof(packet.content));
            memcpy(packet.content, data_ptr + mapped_offset, chunk_size);
            packet.length = chunk_size;
            packet.status = 0;
            
            if(send_packet(sockfd, &packet) == -1)
            {
                munmap(mapped, remaining);
                close(fd);
                return -1;
            }
            
            mapped_offset += chunk_size;
            current_pos += chunk_size;
            
            // 每传输10MB打印一次进度
            if(current_pos % (10 * 1024 * 1024) < chunk_size || current_pos == filesize)
            {
                printf("Upload progress: %ld/%ld bytes (%.1f%%)\n", 
                       current_pos, filesize, (double)current_pos * 100.0 / filesize);
            }
        }
        
        // 清理mmap
        if(munmap(mapped, remaining) == -1)
        {
            perror("munmap failed");
            close(fd);
            return -1;
        }
        
        printf("mmap transfer completed.\n");
    }
    else
    {
        // 小文件使用传统read方式
        printf("Small file (%ld bytes < %d), using standard read...\n", filesize, MMAP_THRESHOLD);
        
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
    }

    packet.status = 0;
    packet.length = 0;
    if(send_packet(sockfd, &packet) == -1)
    {
        close(fd);
        return -1;
    }

    close(fd);
    printf("send complete!\n");

    if(recv_packet(sockfd, &final_packet) == -1)
        return -1;

    if(final_packet.type != PUTS)
        return -1;

    printf("%.*s", final_packet.length, final_packet.content);
    if(final_packet.length > 0 && final_packet.content[final_packet.length - 1] != '\n')
        printf("\n");

    return final_packet.status == 0 ? 0 : -1;
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