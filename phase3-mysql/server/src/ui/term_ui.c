#include "ui/term_ui.h"

#include <stdio.h>

#include "common/utils.h"

#define TERM_UI_TIME_LEN 32

void term_ui_print_banner(void)
{
    puts("");
    puts("   ________                __   ____       _           ");
    puts("  / ____/ /___  __  ______/ /  / __ \\_____(_)   _____  ");
    puts(" / /   / / __ \\/ / / / __  /  / / / / ___/ / | / / _ \\ ");
    puts("/ /___/ / /_/ / /_/ / /_/ /  / /_/ / /  / /| |/ /  __/ ");
    puts("\\____/_/\\____/\\__,_/\\__,_/  /_____/_/  /_/ |___/\\___/  ");
    puts("");
}

void term_ui_print_start_time(void)
{
    char time_text[TERM_UI_TIME_LEN];

    utils_format_current_time(time_text, sizeof(time_text));
    if (time_text[0] == '\0') {
        puts("Cloud Drive starting...");
        return;
    }

    printf("Cloud Drive starting at %s\n\n", time_text);
}
