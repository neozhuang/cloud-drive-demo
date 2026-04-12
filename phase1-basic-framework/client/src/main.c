/*
 * % ============================================================================
 * Author       : neozhuang
 * Email        : zhuangzhuangzhang97@gmail.com
 * CreateTime   : 2026-04-01 19:15:13 +0800
 * LastEditTime : 2026-04-02 15:02:57 +0800
 * Github       : https://github.com/neozhuang/
 * FilePath     : cloud-drive-demo/phase1-basic-framework/client/src/main.c
 * Description  : client main
 * % ============================================================================
 */

#include "../include/common.h"
#include "../include/network.h"
#include "../include/deal_command.h"

/* Usage: ./client [server_ip] [server_port]*/
int main(int argc, char *argv[]) {
    // args check
    ARGS_CHECK(argc, 3);

    // connect server
    int sockfd = tcp_connect(argv[1], argv[2]);
    printf("Connection succeeded! sockfd = %d\n\n", sockfd);

    // dead loop
    while(1) {
        deal_command(sockfd);
    }
    close(sockfd);
    return 0;
}
