#include "../include/config.h"
#include "../include/common.h"

// 去除字符串首尾空白字符
void trim(char *str) {
    int i = 0, j = 0;
    while (str[i] == ' ' || str[i] == '\t') i++;
    while (str[i] != '\0') {
        str[j++] = str[i++];
    }
    str[j] = '\0';
    while (j > 0 && (str[j-1] == ' ' || str[j-1] == '\t' || str[j-1] == '\n')) {
        str[--j] = '\0';
    }
}

// 解析配置文件
int parse_config(const char *filename, config_t *pconfig) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("无法打开配置文件");
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        trim(line);  // 去除首尾空白

        // 跳过空行和注释
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        // 分割 key=value
        char *key = strtok(line, "=");
        char *value = strtok(NULL, "=");
        if (!key || !value) continue;

        trim(key);
        trim(value);

        // 根据 key 存储到结构体
        if (strcmp(key, "server_ip") == 0) {
            strncpy(pconfig->server_ip, value, sizeof(pconfig->server_ip) - 1);
            pconfig->server_ip[sizeof(pconfig->server_ip) - 1] = '\0';  // 确保字符串以 null 结尾
        } else if (strcmp(key, "port") == 0) {
            pconfig->port = atoi(value);
        } else if (strcmp(key, "thread_count") == 0) {
            pconfig->thread_count = atoi(value);
        }
    }

    fclose(file);
    return 0;
}

// 读取配置文件
int read_config(int argc, char *argv[], config_t *pconfig) {
    int ret = 0;
    if(argc == 1) {
        // 解析指定路径配置文件 ./server
        ret =parse_config("./server.conf", pconfig);
        if (ret != 0) {
            printf("parse config file failed!\n");
            return -1;
        }
    } else if(argc == 2) {
         // 解析命令行参数配置文件, ./server *.conf 
        ret = parse_config(argv[1], pconfig);
        if (ret != 0) {
            printf("parse config file failed!\n");
            return -1;
        }
    } else if (argc == 4) {
        // 解析命令行参数, ./server 127.0.0.1 8888 4
        strncpy(pconfig->server_ip, argv[1], sizeof(pconfig->server_ip) - 1);
        pconfig->server_ip[sizeof(pconfig->server_ip) - 1] = '\0';
        pconfig->port = atoi(argv[2]);
        pconfig->thread_count = atoi(argv[3]);
    } else {
        printf("args error!\n");
        return -1;
    }
    return 0;
}
