#include "../include/user_auth.h"
#include "../include/packet.h"
#include "../include/utils.h"
#include "../include/common.h"
#include "../include/transmit_data.h"
#include <termios.h>
#include <stdbool.h>

int user_login(int sockfd, char* username, int len)
{
    char* passwd;
    int flag;

    while(1)
    {
        // 1. 终端输入用户名和密码
        printf("Please enter your username (empty line to quit): ");
        if(s_gets(username, len) == NULL || username[0] == '\0') {
            printf("bye\n");
            close(sockfd);
            exit(-1);
        }
        if((passwd = get_passwd("Please enter your password (empty line to quit): ") ) == NULL || passwd[0] == '\0') {
            printf("bye\n");
            close(sockfd);
            exit(-2);
        }
        // 2. 发送用户名和密码给服务端，明文传输，后续版本会加密
        packet_t packet;
        memset(&packet, 0, sizeof(packet));
        packet.type = LOGIN;
        packet.length = strlen(username);
        strcpy(packet.content, username);
        send_packet(sockfd, &packet);

        memset(packet.content, 0, sizeof(packet.content));
        packet.length = strlen(passwd);
        strcpy(packet.content, passwd);
        send_packet(sockfd, &packet);
        free(passwd);

        // 3. 服务端验证后，判断用户名和密码是否正确
        recv_packet(sockfd, &packet);
        flag = packet.status;
        if(flag == 0){
            printf("Welcome %s to cloud-drive!\n", username);
            break;
        }
        else if(flag == 1) {
            printf("username is not exist.\n");
        }
        else if(flag == 2) {
            printf("password is not correct.\n");
        }
        else {
            printf("unknown error.\n");
        }
    }
    return 0;
}

char* get_passwd(const char* prompt)
{
    printf("%s", prompt);
    set_echo(false); // 关闭回显
    char* passwd = (char*)malloc(32);
    s_gets(passwd, 32);
    set_echo(true); // 恢复回显
    printf("\n");   // 恢复回显时换行符不显示，手动补一个
    return passwd;
}