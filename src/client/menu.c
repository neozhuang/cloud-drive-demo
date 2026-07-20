#include "client/menu.h"

#include <stdio.h>
#include <stdlib.h>

#include "common/utils.h"
#include "common/tui.h"

void menu_draw_border(void)
{
    printf(COLOR_BLUE "+------------------------------------------------+\n"
           COLOR_RESET);
}

void menu_draw_item(const char *item)
{
    printf(COLOR_BLUE "| " COLOR_RESET 
           "                  %-28s"
           COLOR_BLUE " |\n" COLOR_RESET,
           item);
}

int menu_show_auth(void)
{
    int choice;
    char str_choice[16];

    printf(COLOR_BLUE "==================================================\n");
    printf("|" COLOR_CYAN "             Cloud Drive Demo Client            "
           COLOR_BLUE "|\n");
    printf("==================================================\n" COLOR_RESET);
    menu_draw_border();
    menu_draw_item("1. Login");
    menu_draw_item("2. Register");
    menu_draw_item("0. Exit");
    menu_draw_border();

    printf("Please choose: ");
    if (s_gets(str_choice, sizeof(str_choice)) == NULL) {
        return -1;
    }
    choice = strtol(str_choice, NULL, 10);

    return choice;
}

void menu_show_help()
{
    printf(COLOR_BLUE "\n+------------------------------------------------+\n");
    printf("|" COLOR_CYAN "              Cloud Drive Commands             "
           COLOR_BLUE "|\n");
    printf("+------------------------------------------------+\n" COLOR_RESET);
    printf(COLOR_BLUE "| " COLOR_YELLOW "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "pwd", "show current remote path");
    printf(COLOR_BLUE "| " COLOR_YELLOW "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "cd <dir>", "change remote directory");
    printf(COLOR_BLUE "| " COLOR_YELLOW "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "ls", "list remote files");
    printf(COLOR_BLUE "| " COLOR_YELLOW "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "mkdir <dir>", "create remote directory");
    printf(COLOR_BLUE "| " COLOR_YELLOW "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "rmdir <dir>", "remove remote directory");
    printf(COLOR_BLUE "| " COLOR_YELLOW "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "rm <file>", "remove remote file");
    printf(COLOR_BLUE "| " COLOR_YELLOW "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "puts <local>", "upload to remote cwd");
    printf(COLOR_BLUE "| " COLOR_YELLOW "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "gets <remote>", "download in background");
    printf(COLOR_BLUE "+------------------------------------------------+\n");
    printf("| " COLOR_GREEN "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "help | h", "show this command menu");
    printf(COLOR_BLUE "| " COLOR_GREEN "%-14s" COLOR_RESET
           " %-31s" COLOR_BLUE "|\n" COLOR_RESET,
           "quit | exit", "exit cloud drive client");
    printf(COLOR_BLUE "+------------------------------------------------+\n\n" COLOR_RESET);
}
