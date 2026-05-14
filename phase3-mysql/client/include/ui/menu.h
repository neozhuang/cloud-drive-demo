#ifndef CLIENT_UI_MENU_H
#define CLIENT_UI_MENU_H

#include "domain/session.h"

typedef enum ClientMenuChoice {
    CLIENT_MENU_INVALID = 0,
    CLIENT_MENU_LOGIN = 1,
    CLIENT_MENU_REGISTER = 2,
    CLIENT_MENU_DELETE = 3,
    CLIENT_MENU_EXIT = 4
} ClientMenuChoice;

ClientMenuChoice client_term_ui_read_menu_choice(void);
void client_term_ui_print_menu(void);
int client_term_ui_run_main_menu(ClientContext *context);

#endif
