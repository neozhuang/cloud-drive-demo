#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdio.h>  //stderr
#include <stdlib.h> //EXIT_FAILURE
#include <unistd.h> // close
#include <fcntl.h> // open

#include <sys/stat.h> // fstat stat

#include <string.h> // memset

#include <linux/limits.h> // PATH_MAX

// 检查命令行参数数量是否符合预期
#define ARGS_CHECK(argc, expected) \
    do { \
        if ((argc) != (expected)) { \
            fprintf(stderr, "args num error!\n");\
            exit(EXIT_FAILURE);\
        } \
    } while (0)

// 检查返回值是否是错误标记,若是则打印msg和错误信息
#define ERROR_CHECK(ret, error_flag, msg) \
    do { \
        if ((ret) == (error_flag)) { \
            perror(msg); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

#endif /* __COMMON_H__ */
