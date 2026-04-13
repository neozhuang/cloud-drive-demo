#ifndef __USER_AUTH_H__
#define __USER_AUTH_H__

int user_login(int sockfd, char* username, int len); 
char* get_passwd(const char* prompt);

#endif /* __USER_AUTH_H__ */