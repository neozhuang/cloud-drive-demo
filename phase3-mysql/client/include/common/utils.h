#ifndef __CLIENT_UTILS_H__
#define __CLIENT_UTILS_H__

#include <stddef.h>

void client_utils_copy_string(char *dest, size_t dest_size, const char *src);
int client_utils_parse_int(const char *value, int *out);
void client_utils_format_current_time(char *dest, size_t dest_size);
int client_utils_read_line(const char *prompt, char *buffer, size_t buffer_size);

#endif
