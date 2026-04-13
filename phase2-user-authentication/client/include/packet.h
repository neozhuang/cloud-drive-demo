#ifndef __PACKET_H__
#define __PACKET_H__

#define CONTENT_MAX 1024

typedef enum {
    PROMPT,
    PWD,
    CD,
    LS,
    LL,
    MKDIR,
    RMDIR,
    RM,
    PUTS,
    GETS,
    TREE,
    CAT,
    LOGIN,
    LOGOUT,
    NOTCMD,
} cmd_type_t;

typedef struct 
{
    cmd_type_t type; // 命令类型
    int status; // 0 成功，-1 失败
    int length; //记录命令长度
    char content[CONTENT_MAX]; //记录命令内容
} packet_t;

#endif /* __PACKET_H__ */
