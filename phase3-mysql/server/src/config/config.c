#include "config/config.h"

#include <stdio.h>
#include <string.h>

#include "ini.h"
#include "common/utils.h"

#define MATCH(section_name, key_name) \
    (strcmp(section, (section_name)) == 0 && strcmp(name, (key_name)) == 0)

static int validate_config(const ServerConfig *config)
{
    if (config->listen.port <= 0 || config->listen.port > 65535) {
        fprintf(stderr, "Invalid server.port: %d\n", config->listen.port);
        return 0;
    }

    if (config->listen.backlog <= 0) {
        fprintf(stderr, "Invalid server.backlog: %d\n", config->listen.backlog);
        return 0;
    }

    if (config->mysql.port <= 0 || config->mysql.port > 65535) {
        fprintf(stderr, "Invalid mysql.port: %d\n", config->mysql.port);
        return 0;
    }

    if (config->thread_pool.worker_threads <= 0) {
        fprintf(stderr, "Invalid thread_pool.worker_threads: %d\n",
                config->thread_pool.worker_threads);
        return 0;
    }

    if (config->thread_pool.queue_capacity <= 0) {
        fprintf(stderr, "Invalid thread_pool.queue_capacity: %d\n",
                config->thread_pool.queue_capacity);
        return 0;
    }

    return 1;
}

static int config_ini_handler(void *user, const char *section, const char *name,
                              const char *value)
{
    ServerConfig *config = (ServerConfig *)user;
    int ignored_int;

    if (MATCH("server", "host")) {
        utils_copy_string(config->listen.host, sizeof(config->listen.host), value);
    } else if (MATCH("server", "port")) {
        return utils_parse_int(value, &config->listen.port);
    } else if (MATCH("server", "backlog")) {
        return utils_parse_int(value, &config->listen.backlog);
    } else if (MATCH("mysql", "host")) {
        utils_copy_string(config->mysql.host, sizeof(config->mysql.host), value);
    } else if (MATCH("mysql", "port")) {
        return utils_parse_int(value, &config->mysql.port);
    } else if (MATCH("mysql", "user")) {
        utils_copy_string(config->mysql.user, sizeof(config->mysql.user), value);
    } else if (MATCH("mysql", "password")) {
        utils_copy_string(config->mysql.password, sizeof(config->mysql.password), value);
    } else if (MATCH("mysql", "database")) {
        utils_copy_string(config->mysql.database, sizeof(config->mysql.database), value);
    } else if (MATCH("mysql", "charset")) {
        utils_copy_string(config->mysql.charset, sizeof(config->mysql.charset), value);
    } else if (MATCH("mysql", "pool_size")) {
        return utils_parse_int(value, &ignored_int);
    } else if (MATCH("storage", "root_dir")) {
        utils_copy_string(config->storage.root_dir, sizeof(config->storage.root_dir),
                          value);
    } else if (MATCH("thread_pool", "worker_threads")) {
        return utils_parse_int(value, &config->thread_pool.worker_threads);
    } else if (MATCH("thread_pool", "queue_capacity")) {
        return utils_parse_int(value, &config->thread_pool.queue_capacity);
    } else if (MATCH("log", "level")) {
        utils_copy_string(config->log.level, sizeof(config->log.level), value);
    } else if (MATCH("log", "file")) {
        utils_copy_string(config->log.file, sizeof(config->log.file), value);
    } else {
        return 0;
    }

    return 1;
}

static int config_try_parse(const char *path, ServerConfig *config)
{
    return ini_parse(path, config_ini_handler, config);
}

void config_init_defaults(ServerConfig *config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));

    utils_copy_string(config->listen.host, sizeof(config->listen.host), "0.0.0.0");
    config->listen.port = 9000;
    config->listen.backlog = 128;

    utils_copy_string(config->mysql.host, sizeof(config->mysql.host), "127.0.0.1");
    config->mysql.port = 3306;
    utils_copy_string(config->mysql.user, sizeof(config->mysql.user), "cloud_drive");
    utils_copy_string(config->mysql.password, sizeof(config->mysql.password),
                      "cloud_drive_password");
    utils_copy_string(config->mysql.database, sizeof(config->mysql.database),
                      "cloud_drive");
    utils_copy_string(config->mysql.charset, sizeof(config->mysql.charset),
                      "utf8mb4");

    utils_copy_string(config->storage.root_dir, sizeof(config->storage.root_dir),
                      "./data/files");

    config->thread_pool.worker_threads = 4;
    config->thread_pool.queue_capacity = 1024;

    utils_copy_string(config->log.level, sizeof(config->log.level), "info");
    utils_copy_string(config->log.file, sizeof(config->log.file), "./logs/server.log");
}

int config_load(ServerConfig *config, const char *path)
{
    int ret;
    const char *config_path = path == NULL ? CONFIG_DEFAULT_PATH : path;
    const char *fallback_path = "server/" CONFIG_DEFAULT_PATH;
    int use_fallback = 0;

    if (config == NULL) {
        return -1;
    }

    config_init_defaults(config);

    ret = config_try_parse(config_path, config);
    if (ret < 0 && path == NULL && strcmp(config_path, CONFIG_DEFAULT_PATH) == 0) {
        config_init_defaults(config);
        ret = config_try_parse(fallback_path, config);
        if (ret >= 0) {
            config_path = fallback_path;
            use_fallback = 1;
        }
    }

    if (ret < 0) {
        fprintf(stderr, "Cannot open config file: %s\n", config_path);
        return -1;
    }

    if (ret > 0) {
        fprintf(stderr, "Config parse error near line: %d\n", ret);
        return -1;
    }

    if (use_fallback) {
        fprintf(stderr, "Server config fallback path in use: %s\n", config_path);
    }

    return validate_config(config) ? 0 : -1;
}

void config_print(const ServerConfig *config)
{
    if (config == NULL) {
        return;
    }

    printf("[server]\n");
    printf("host = %s\n", config->listen.host);
    printf("port = %d\n", config->listen.port);
    printf("backlog = %d\n", config->listen.backlog);

    printf("\n[mysql]\n");
    printf("host = %s\n", config->mysql.host);
    printf("port = %d\n", config->mysql.port);
    printf("user = %s\n", config->mysql.user);
    printf("database = %s\n", config->mysql.database);
    printf("charset = %s\n", config->mysql.charset);

    printf("\n[storage]\n");
    printf("root_dir = %s\n", config->storage.root_dir);

    printf("\n[thread_pool]\n");
    printf("worker_threads = %d\n", config->thread_pool.worker_threads);
    printf("queue_capacity = %d\n", config->thread_pool.queue_capacity);

    printf("\n[log]\n");
    printf("level = %s\n", config->log.level);
    printf("file = %s\n", config->log.file);
}
