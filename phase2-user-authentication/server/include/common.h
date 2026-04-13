#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>

// 网络编程
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/epoll.h>

#include <crypt.h>
#include <shadow.h>
#include <stdbool.h>
#include <sys/sendfile.h>
#include <libgen.h>  // 提供 dirname() 函数（POSIX 系统）

#include <linux/limits.h> // PATH_MAX

// 检查命令行参数数量是否符合预期
#define ARGS_CHECK(argc, expected) \
    do { \
        if ((argc) != (expected)) { \
            fprintf(stderr, "args num error!\n");\
            exit(1); \
        } \
    } while (0)                      

// 检查返回值是否是错误标记,若是则打印msg和错误信息
#define ERROR_CHECK(ret, error_flag, msg) \
    do { \
        if ((ret) == (error_flag)) { \
            perror(msg); \
            exit(1); \
        } \
    } while (0)

// 将pthread库函数检查报错的流程封装成类似于ERROR_CHECK的带参数宏定义。
#define THREAD_ERROR_CHECK(ret,msg) \
    do { \
        if (ret != 0) { \
            fprintf(stderr,"%s:%s\n",msg, strerror(ret)); \
            exit(-1); \
        } \
    } while (0)   

#endif /* __COMMON_H__ */
