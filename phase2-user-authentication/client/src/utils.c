#include "utils.h"
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

char* s_gets(char* st, int n)
{
    char* find;
    char* ret_val = fgets(st, n, stdin);
    if(ret_val)
    {
        find = strchr(st,'\n');
        if(find) *find = '\0';
        else
        {
            while(getchar() != '\n')
                continue;
        }   
    }
    return ret_val;
}

void set_echo(bool enable)
{
    struct termios tty;
    tcgetattr(STDIN_FILENO, &tty);
    if(enable) tty.c_lflag |= ECHO;
    else tty.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &tty);
}