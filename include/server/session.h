#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct session session_t;

int session_table_init(void);

int session_table_destroy(void);

/**
 * @brief Create and register a session for a connected client.
 *
 * @param client_fd Client socket file descriptor used as the session key.
 * @return Pointer to the created session, or NULL on failure.
 */
session_t *session_create(int client_fd);

void session_destroy(int client_fd);

int session_set_login_state(int client_fd,
                            uint64_t user_id,
                            const char *username,
                            uint64_t root_path_id);


/**
 * @brief Get the username stored in a client session.
 */
int session_get_username(int client_fd, char* username, int size);

int session_set_cwd(int client_fd, uint64_t cwd_id, const char *cwd);
int session_get_cwd(int client_fd, char *cwd, size_t size);

int session_get_location(int client_fd, uint64_t* user_id, uint64_t* cwd_id, char* cwd, int size);

