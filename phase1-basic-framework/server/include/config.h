#ifndef __CONFIG_H__
#define __CONFIG_H__

typedef struct {
    char server_ip[16];  // IPv4 最长 15 字符
    int port;          // 端口号
    int thread_count;   // 线程池线程数
} config_t;

// 去除字符串首尾空白字符
void trim(char *str);

// 解析配置文件
int parse_config(const char *filename, config_t *config);

// 读取配置文件
int read_config(int argc, char *argv[], config_t *config);

#endif /* __CONFIG_H__ */
