#include "ui/menu.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ui/term_ui.h"
#include "common/utils.h"
#include "service/auth_service.h"

#define CLIENT_TERM_UI_INPUT_LEN 32

static void client_term_ui_clear_screen(void)
{
    printf("\033[2J\033[H");
}

void client_term_ui_print_menu(void)
{
    printf(COLOR_BLUE "==================================================\n");
    printf("|" COLOR_CYAN "             Cloud Drive Demo Client            "
           COLOR_BLUE "|\n");
    printf("==================================================\n" COLOR_RESET);
    client_term_ui_draw_menu_border();
    client_term_ui_draw_menu_item("1. 用户登录");
    client_term_ui_draw_menu_item("2. 用户注册");
    client_term_ui_draw_menu_item("3. 用户删除");
    client_term_ui_draw_menu_item("4. 退出系统");
    client_term_ui_draw_menu_border();
}

ClientMenuChoice client_term_ui_read_menu_choice(void)
{
    char input[CLIENT_TERM_UI_INPUT_LEN];

    client_term_ui_print_menu();
    if (client_utils_read_line("请选择: ", input, sizeof(input)) != 0) {
        return CLIENT_MENU_EXIT;
    }

    if (strcmp(input, "1") == 0) {
        return CLIENT_MENU_LOGIN;
    }

    if (strcmp(input, "2") == 0) {
        return CLIENT_MENU_REGISTER;
    }

    if (strcmp(input, "3") == 0) {
        return CLIENT_MENU_DELETE;
    }

    if (strcmp(input, "4") == 0) {
        return CLIENT_MENU_EXIT;
    }

    return CLIENT_MENU_INVALID;
}

int client_term_ui_run_main_menu(ClientContext *context)
{
    int ret;
    while (1) {
        ClientMenuChoice choice = client_term_ui_read_menu_choice();

        switch (choice) {
            case CLIENT_MENU_LOGIN:
                ret = client_user_login(context);
                if(ret == 0) return 0;
                else break;
            case CLIENT_MENU_REGISTER:
                client_user_register(context);
                break;
            case CLIENT_MENU_DELETE:
                client_user_delete(context);
                break;
            case CLIENT_MENU_EXIT:
                printf(COLOR_CYAN "\n退出系统，再见！\n"
                       COLOR_RESET);
                return 0;
            default:
                printf(COLOR_RED
                       "\n输入无效，请输入 1、2、3 或 4。\n"
                       COLOR_RESET);
                sleep(1);
                break;
        }
        sleep(1);
        client_term_ui_clear_screen();
    }
}
