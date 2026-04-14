#include "../include/deal_command.h"
#include "../include/command_handlers.h"
#include "../include/utils.h"
#include "../include/packet.h"
#include "../include/transmit_data.h"
#include <linux/limits.h>
#include <string.h>
#include <stdio.h>

static cmd_type_t str2cmd(const char* cmd)
{
    if(strcmp(cmd,"pwd") == 0) return PWD;
    else if(strcmp(cmd, "cd") == 0) return CD;
    else if(strcmp(cmd, "ls") == 0) return LS;
    else if(strcmp(cmd, "ll") == 0) return LL;
    else if(strcmp(cmd, "mkdir") == 0) return MKDIR;
    else if(strcmp(cmd, "rmdir") == 0) return RMDIR;
    else if(strcmp(cmd, "rm") == 0) return RM;
    else if(strcmp(cmd, "puts") == 0) return PUTS;
    else if(strcmp(cmd, "gets") == 0) return GETS;
    else if(strcmp(cmd, "tree") == 0) return TREE;
    else if(strcmp(cmd, "cat") == 0) return CAT;
    else if(strcmp(cmd, "login") == 0) return LOGIN;
    else if(strcmp(cmd, "exit") == 0) return CLIENT_EXIT;
    else return NOTCMD;
}

static void init_prompt(char *prompt)
{
    static int initialized = 0;
    if(initialized) return;
    snprintf(prompt, PATH_MAX, "%s",  "~");
    initialized = 1;
}

static void print_prompt(char *prompt, const char* username)
{
    init_prompt(prompt);
    if(strcmp(prompt, username) == 0)
        printf("%s@%s:~$ ", username, SERVICE_NAME);
    else
        printf("%s@%s:%s$ ", username, SERVICE_NAME, prompt);
    fflush(stdout);
}

static int parse_command(packet_t *request)
{
    char input[CONTENT_MAX] = {0};
    char *command = NULL;
    char *parameter = NULL;

    memset(request, 0, sizeof(*request));
    if(s_gets(input, sizeof(input)) == NULL)
        return -1;

    command = strtok(input, " ");
    if(command == NULL)
    {
        request->type = NOTCMD;
        return 0;
    }

    request->type = str2cmd(command);
    parameter = strtok(NULL, "");
    if(parameter != NULL)
    {
        snprintf(request->content, sizeof(request->content), "%s", parameter);
        request->length = strlen(request->content);
    }
    return 0;
}

int deal_command(int sockfd, const char* username) {
    static char prompt[PATH_MAX] = {0};
    packet_t request;
    packet_t response;
    // 打印提示符
    print_prompt(prompt, username);
    // 解析用户输入并构造请求包
    if(parse_command(&request) == -1) return -1;
    // 发送命令请求给服务器
    if(send_packet(sockfd, &request) == -1) return -1;

    // 如果是puts命令，先上传文件内容，再等待服务器响应
    if(request.type == PUTS && puts_file_client(sockfd, request.content) == -1)
        return -1;
    // 接收服务器响应
    if(recv_packet(sockfd, &response) == -1) return -1;
    // 处理服务器响应并打印结果
    return handle_response(sockfd, &response, prompt, request.content);
}