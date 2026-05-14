#include "common/utils.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void utils_copy_string(char *dest, size_t dest_size, const char *src)
{
    if (dest_size == 0) {
        return;
    }

    if (src == NULL) {
        dest[0] = '\0';
        return;
    }

    snprintf(dest, dest_size, "%s", src);
}

int utils_parse_int(const char *value, int *out)
{
    char *end = NULL;
    long parsed;

    if (value == NULL || *value == '\0' || out == NULL) {
        return 0;
    }

    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed < INT_MIN ||
        parsed > INT_MAX) {
        return 0;
    }

    *out = (int)parsed;
    return 1;
}

void utils_format_current_time(char *dest, size_t dest_size)
{
    time_t now;
    struct tm *local_time;

    if (dest_size == 0) {
        return;
    }

    now = time(NULL);
    local_time = localtime(&now);
    if (local_time == NULL ||
        strftime(dest, dest_size, "%Y-%m-%d %H:%M:%S", local_time) == 0) {
        dest[0] = '\0';
    }
}
