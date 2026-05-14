#ifndef CLOUD_DRIVE_SESSION_H
#define CLOUD_DRIVE_SESSION_H

#include <stdint.h>

typedef struct Session {
    int client_fd;
    uint64_t user_id;
    struct Session *next;
} Session;

Session *session_create(int client_fd);
Session *session_find(int client_fd);
void session_destroy(int client_fd);
void session_destroy_all(void);
int session_set_user(Session *session, uint64_t user_id);

#endif
