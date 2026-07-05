#include "common/tui.h"
#include "common/utils.h"

#include <stdio.h>

#define TUI_TIME_LEN 32

void tui_print_banner(void)
{
    printf(COLOR_GREEN "\n");
    puts("   ________                __   ____       _           ");
    puts("  / ____/ /___  __  ______/ /  / __ \\_____(_)   _____  ");
    puts(" / /   / / __ \\/ / / / __  /  / / / / ___/ / | / / _ \\ ");
    puts("/ /___/ / /_/ / /_/ / /_/ /  / /_/ / /  / /| |/ /  __/ ");
    puts("\\____/_/\\____/\\__,_/\\__,_/  /_____/_/  /_/ |___/\\___/  ");
    printf("\n" COLOR_RESET);
}

void tui_print_time(const char* who)
{
    char time_text[TUI_TIME_LEN];

    utils_format_current_time(time_text, sizeof(time_text));

    printf(COLOR_GREEN "%s starting at %s\n\n"
           COLOR_RESET,who, time_text);
}

void tui_clear_screen(void)
{
    printf("\033[2J\033[H");
}

