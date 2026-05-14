#ifndef CLOUD_DRIVE_UTILS_H
#define CLOUD_DRIVE_UTILS_H

#include <stddef.h>

void utils_copy_string(char *dest, size_t dest_size, const char *src);
int utils_parse_int(const char *value, int *out);
void utils_format_current_time(char *dest, size_t dest_size);

#endif
