#pragma once

#define MAX_AUTH_USERS 128
#define MAX_USERNAME_LEN 64

typedef struct {
    char username[MAX_USERNAME_LEN];
} auth_user_t;

typedef struct {
    auth_user_t users[MAX_AUTH_USERS];
    int user_count;
} auth_config_t;

int auth_config_load(auth_config_t *config, const char *path);
int auth_check_user(const auth_config_t *config, const char *username);
