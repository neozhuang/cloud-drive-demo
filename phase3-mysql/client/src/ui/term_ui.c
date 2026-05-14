#include "ui/term_ui.h"

#include <stdio.h>

#include "common/utils.h"

#define TERM_UI_TIME_LEN 32

void client_term_ui_print_banner(void)
{
    printf(COLOR_GREEN "\n");
    puts("   ________                __   ____       _           ");
    puts("  / ____/ /___  __  ______/ /  / __ \\_____(_)   _____  ");
    puts(" / /   / / __ \\/ / / / __  /  / / / / ___/ / | / / _ \\ ");
    puts("/ /___/ / /_/ / /_/ / /_/ /  / /_/ / /  / /| |/ /  __/ ");
    puts("\\____/_/\\____/\\__,_/\\__,_/  /_____/_/  /_/ |___/\\___/  ");
    printf("\n" COLOR_RESET);
}

void client_term_ui_print_start_time(void)
{
    char time_text[TERM_UI_TIME_LEN];

    client_utils_format_current_time(time_text, sizeof(time_text));
    if (time_text[0] == '\0') {
        puts("Cloud Drive Client starting...");
        return;
    }

    printf(COLOR_GREEN "Cloud Drive Client starting at %s\n\n"
           COLOR_RESET,
           time_text);
}

void client_term_ui_draw_menu_border(void)
{
    printf(COLOR_BLUE "+------------------------------------------------+\n"
           COLOR_RESET);
}

void client_term_ui_draw_menu_item(const char *item)
{
    printf(COLOR_BLUE "| " COLOR_RESET 
           "                  %-32s"
           COLOR_BLUE " |\n" COLOR_RESET,
           item);
}
