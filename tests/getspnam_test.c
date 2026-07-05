#include <shadow.h>
#include <stdio.h>
#include <stdlib.h>

int main() 
{
    const char *username = "zhuang";
    struct spwd *pwd = getspnam(username);
    if (pwd == NULL)
    {
        printf("username %s not exist\n", username);
        return 0;
    }

    printf("UserName: %s\n", pwd->sp_namp);
    printf("Password: %s\n", pwd->sp_pwdp);

    return 0;
}

