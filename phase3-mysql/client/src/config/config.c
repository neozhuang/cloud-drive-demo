#include "config/config.h"

#include <stdio.h>
#include <string.h>

#include "ini.h"
#include "common/utils.h"

#define MATCH(section_name, key_name) \
    (strcmp(section, (section_name)) == 0 && strcmp(name, (key_name)) == 0)

static int client_config_validate(const ClientConfig *config)
{
    if (config->server.host[0] == '\0') {
        fprintf(stderr, "Invalid server.host: empty value\n");
        return 0;
    }

    if (config->server.port <= 0 || config->server.port > 65535) {
        fprintf(stderr, "Invalid server.port: %d\n", config->server.port);
        return 0;
    }

    return 1;
}

static int client_config_ini_handler(void *user, const char *section,
                                     const char *name, const char *value)
{
    ClientConfig *config = (ClientConfig *)user;

    if (MATCH("server", "host")) {
        client_utils_copy_string(config->server.host, sizeof(config->server.host),
                                 value);
    } else if (MATCH("server", "port")) {
        return client_utils_parse_int(value, &config->server.port);
    } else if (MATCH("log", "level")) {
        client_utils_copy_string(config->log.level, sizeof(config->log.level),
                                 value);
    } else if (MATCH("log", "file")) {
        client_utils_copy_string(config->log.file, sizeof(config->log.file),
                                 value);
    } else {
        return 0;
    }

    return 1;
}

static int client_config_try_parse(const char *path, ClientConfig *config)
{
    return ini_parse(path, client_config_ini_handler, config);
}

void client_config_init_defaults(ClientConfig *config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    client_utils_copy_string(config->server.host, sizeof(config->server.host),
                             "127.0.0.1");
    config->server.port = 8888;
    client_utils_copy_string(config->log.level, sizeof(config->log.level), "info");
    client_utils_copy_string(config->log.file, sizeof(config->log.file),
                             "./logs/client.log");
}

int client_config_load(ClientConfig *config, const char *path)
{
    int ret;
    const char *config_path = path == NULL ? CLIENT_CONFIG_DEFAULT_PATH : path;
    const char *fallback_path = "client/" CLIENT_CONFIG_DEFAULT_PATH;
    int use_fallback = 0;

    if (config == NULL) {
        return -1;
    }

    client_config_init_defaults(config);

    ret = client_config_try_parse(config_path, config);
    if (ret < 0 && path == NULL && strcmp(config_path, CLIENT_CONFIG_DEFAULT_PATH) == 0) {
        client_config_init_defaults(config);
        ret = client_config_try_parse(fallback_path, config);
        if (ret >= 0) {
            config_path = fallback_path;
            use_fallback = 1;
        }
    }

    if (ret < 0) {
        fprintf(stderr, "Cannot open client config file: %s\n", config_path);
        return -1;
    }

    if (ret > 0) {
        fprintf(stderr, "Client config parse error near line: %d\n", ret);
        return -1;
    }

    if (use_fallback) {
        fprintf(stderr, "Client config fallback path in use: %s\n", config_path);
    }

    return client_config_validate(config) ? 0 : -1;
}

void client_config_print(const ClientConfig *config)
{
    if (config == NULL) {
        return;
    }

    printf("[server]\n");
    printf("host = %s\n", config->server.host);
    printf("port = %d\n", config->server.port);

    printf("\n[log]\n");
    printf("level = %s\n", config->log.level);
    printf("file = %s\n", config->log.file);
}
