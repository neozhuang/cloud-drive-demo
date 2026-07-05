#include <unistd.h>
#include <crypt.h>
#include <stdio.h>

// compile: -lcrypt
int main()
{
    const char *password = ""; // enter your password
    const char *salt = "$y$j9T$8SLo.d5ZolRGxrC1rPMlE0$"; // yescrypt

    char *encrypted = crypt(password, salt);
    if (encrypted == NULL)
    {
        perror("crypt");
        return 0;
    }

    printf("password: %s\n", encrypted);
    return 0;
}
