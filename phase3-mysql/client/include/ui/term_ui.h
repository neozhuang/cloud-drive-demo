#ifndef __TERM_UI_H__
#define __TERM_UI_H__

#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_BLUE "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN "\033[36m"
#define COLOR_RESET "\033[0m"

void client_term_ui_draw_menu_border(void);
void client_term_ui_draw_menu_item(const char *item);
void client_term_ui_print_banner(void);
void client_term_ui_print_start_time(void);

#endif
