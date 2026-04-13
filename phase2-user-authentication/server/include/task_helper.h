#ifndef __TASK_HELPER_H__
#define __TASK_HELPER_H__

#include "common.h"
#include "packet.h"
#include "stack.h"

int send_text_response(int netfd, cmd_type_t type, int status, const char *text);

int push_path_stack(linked_stack_t * p_stack, char * p);
void pop_path_stack(linked_stack_t * p_stack, char * dest_dir);
int get_short_path(char * short_path, const char * cd_dir, const char * session_path);

void format_mode_string(mode_t mode, char *mode_str);
int append_ll_entry(char *buffer, size_t buffer_size, const char *file_path, const char *display_name);
int send_tree_entries(int netfd, const char *base_path, int depth);

#endif /* __TASK_HELPER_H__ */