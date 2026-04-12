#include "utils.h"
#include <stdio.h>
#include <string.h>

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
            while(getchar()!='\n');
        }
    }
    return ret_val;
}