#pragma once


#include <stdint.h>
#include <stddef.h>

typedef struct session session_t;

session_t *session_create(int client_fd);
int session_set_login_state(int client_fd, const char *username, const char *user_root);
int session_get_paths(int client_fd, char *user_root, size_t root_size,
                      char *cwd, size_t cwd_size);
int session_set_cwd(int client_fd, const char *cwd);
char* session_get_cwd(int client_fd);
char* session_get_username(int client_fd);
void session_destroy(int client_fd);
void session_destroy_all(void);
// int session_set_login_state(session_t *session, uint64_t user_id, uint64_t cwd_id, const char* cwd);
