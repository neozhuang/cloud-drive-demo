#include "client/config.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inih/ini.h"

static int copy_string(char *destination, size_t destination_size,
                       const char *value)
{
    size_t length;

    if (destination == NULL || destination_size == 0 || value == NULL) {
        return -1;
    }

    length = strlen(value);
    if (length >= destination_size) {
        return -1;
    }

    memcpy(destination, value, length + 1);
    return 0;
}

static int parse_positive_int(const char *value, int *result)
{
    char *end = NULL;
    long parsed;

    if (value == NULL || result == NULL || value[0] == '\0') {
        return -1;
    }

    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' ||
        parsed <= 0 || parsed > INT_MAX) {
        return -1;
    }

    *result = (int)parsed;
    return 0;
}

static int config_handler(void *user, const char *section, const char *name,
                          const char *value)
{
    client_config_t *config = user;

#define MATCH(s, n) (strcmp(section, (s)) == 0 && strcmp(name, (n)) == 0)
    if (MATCH("remote", "host")) {
        return copy_string(config->remote.host, sizeof(config->remote.host),
                           value) == 0;
    }
    if (MATCH("remote", "port")) {
        return copy_string(config->remote.port, sizeof(config->remote.port),
                           value) == 0;
    }
    if (MATCH("log", "log_level")) {
        return copy_string(config->log.log_level,
                           sizeof(config->log.log_level), value) == 0;
    }
    if (MATCH("log", "log_file")) {
        return copy_string(config->log.log_file, sizeof(config->log.log_file),
                           value) == 0;
    }
    if (MATCH("storage", "download_dir")) {
        return copy_string(config->storage.download_dir,
                           sizeof(config->storage.download_dir), value) == 0;
    }
    if (MATCH("transfer", "max_concurrent")) {
        return parse_positive_int(value,
                                  &config->transfer.max_concurrent) == 0;
    }
    if (MATCH("transfer", "connect_timeout_ms")) {
        return parse_positive_int(value,
                                  &config->transfer.connect_timeout_ms) == 0;
    }
    if (MATCH("transfer", "io_timeout_ms")) {
        return parse_positive_int(value, &config->transfer.io_timeout_ms) == 0;
    }
#undef MATCH

    return 0;
}

int client_config_load(client_config_t *config, const char *path)
{
    client_config_t parsed;
    int result;

    if (config == NULL || path == NULL || path[0] == '\0') {
        return -1;
    }

    memset(&parsed, 0, sizeof(parsed));

    result = ini_parse(path, config_handler, &parsed);
    if (result != 0) {
        fprintf(stderr, "Cannot parse %s", path);
        if (result > 0) {
            fprintf(stderr, " at line %d", result);
        }
        fputc('\n', stderr);
        return -1;
    }

    *config = parsed;
    return 0;
}
