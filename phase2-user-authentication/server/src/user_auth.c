#include "../include/user_auth.h"
#include "../include/common.h"
#include "../include/session.h"
#include "../include/transmit_data.h"

/*
static void get_salt(char* salt, const char* passwd)
{
    int i, j;
    for(i = 0, j = 0; passwd[i] && j < 4; ++i)
    {
        if(passwd[i] == '$')
            j++;
    }
    strncpy(salt, passwd, i);
}
*/

int user_auth(const char *username, const char *password, int netfd)
{
    // 获取当前执行进程的实际用户id，保存便于后面恢复
    uid_t uid = getuid();
    seteuid(0); // 切换到超级用户权限，获取用户信息需要超级用户权限
    struct spwd* sp = getspnam(username);
    if(!sp) {
        printf("user %s is not exist!\n", username);
        return 1; // 用户不存在
    }
    // 进程具有超级用户特权，seteuid将实际用户ID、有效用户ID，取消超级用户权限
    if(seteuid(uid) != 0)
        perror("seteuid");

    // 获取盐值，这里get_salt函数被注释掉了，因为不同加密方式盐值字段不同，需要的get_salt不一样
    // 直接使用sp->sp_pwdp作为加密函数的第二个参数，crypt会自动从中提取盐值
    //char salt[128] = {0};
    //get_salt(salt, sp->sp_pwdp);
    //char *encrypted = crypt(password, salt);

    char *encrypted = crypt(password, sp->sp_pwdp);
    if (encrypted == NULL || strcmp(encrypted, sp->sp_pwdp) != 0) {
        return 2; // 密码错误
    }
    
    // 认证成功，创建/更新会话
    session_t* session = session_get_or_create(netfd);
    if (!session) return -1;
    
    strncpy(session->username, username, sizeof(session->username)-1);
    struct passwd *pw = getpwnam(username);
    session->uid = pw->pw_uid;
    session->gid = pw->pw_gid;
    session->is_authenticated = 1;
    
    // 创建用户专属目录
    char user_dir[PATH_MAX];
    snprintf(user_dir, sizeof(user_dir), "%s", username);
    mkdir(user_dir, 0700); // 设置用户权限
    
    // 初始化当前路径为用户目录
    session_update_path(netfd, user_dir);
    
    return 0; // 成功
}
