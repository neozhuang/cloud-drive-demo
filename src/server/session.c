#include "server/session.h"

#include <limits.h>
#include <linux/limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdio.h>


struct session {
    int client_fd;
    int logged_in;
    uint64_t user_id;
    char username[64];
    char user_root[PATH_MAX];
    uint64_t cwd_id;
    char cwd[PATH_MAX];
    struct session *next;
} ;

static session_t* g_sessions = NULL;
static pthread_mutex_t g_session_lock = PTHREAD_MUTEX_INITIALIZER;

session_t *session_create(int client_fd)
{
    session_t* session = calloc(1, sizeof(session_t));
    if (session == NULL) {
        return NULL;
    }
    session->client_fd = client_fd;
    pthread_mutex_lock(&g_session_lock);
    session->next = g_sessions;
    g_sessions = session;
    pthread_mutex_unlock(&g_session_lock);

    return session;
}

int session_set_login_state(int client_fd, const char *username, const char *user_root)
{
    pthread_mutex_lock(&g_session_lock);

    session_t *session = g_sessions;
    while (session) {
        if (session->client_fd == client_fd) {
            session->logged_in = 1;
            strncpy(session->username, username, sizeof(session->username) - 1);
            session->username[sizeof(session->username) - 1] = '\0';
            strncpy(session->user_root, user_root, sizeof(session->user_root) - 1);
            session->user_root[sizeof(session->user_root) - 1] = '\0';
            session->cwd_id = 0;
            strncpy(session->cwd, "/", sizeof(session->cwd) - 1);

            pthread_mutex_unlock(&g_session_lock);
            return 0;
        }

        session = session->next;
    }

    pthread_mutex_unlock(&g_session_lock);
    return -1;
}

int session_get_paths(int client_fd, char *user_root, size_t root_size,
                      char *cwd, size_t cwd_size)
{
    pthread_mutex_lock(&g_session_lock);
    
    session_t* session = g_sessions;

    while (session) {
        if (session->client_fd == client_fd && session->logged_in) {
            strncpy(user_root, session->user_root, root_size - 1);
            user_root[root_size - 1] = '\0';

            strncpy(cwd, session->cwd, cwd_size - 1);
            cwd[cwd_size - 1] = '\0';

            pthread_mutex_unlock(&g_session_lock);
            return 0;
        }
        session = session->next;
    }

    pthread_mutex_unlock(&g_session_lock);
    return -1;
}

int session_set_cwd(int client_fd, const char *cwd)
{
    pthread_mutex_lock(&g_session_lock);

    session_t* session = g_sessions;

    while (session) {
        if (session->client_fd == client_fd && session->logged_in) {
            strncpy(session->cwd, cwd, sizeof(session->cwd) - 1);
            session->cwd[sizeof(session->cwd) - 1] = '\0';

            pthread_mutex_unlock(&g_session_lock);
            return 0;
        }
        session = session->next;
    }
    pthread_mutex_unlock(&g_session_lock);
    return -1;
}

char* session_get_cwd(int client_fd)
{
    pthread_mutex_lock(&g_session_lock);

    session_t *session = g_sessions;
    while (session) {
        if (session->client_fd == client_fd) {
            pthread_mutex_unlock(&g_session_lock);
            return session->cwd;
        }

        session = session->next;
    }

    pthread_mutex_unlock(&g_session_lock);
    return NULL;
}

char* session_get_username(int client_fd)
{
    pthread_mutex_lock(&g_session_lock);

    session_t *session = g_sessions;
    while (session) {
        if (session->client_fd == client_fd) {
            pthread_mutex_unlock(&g_session_lock);
            return session->username;
        }

        session = session->next;
    }

    pthread_mutex_unlock(&g_session_lock);
    return NULL;
}

// prev curr
void session_destroy(int client_fd)
{
    pthread_mutex_lock(&g_session_lock);

    session_t* prev = NULL;
    session_t* curr = g_sessions;

    while (curr) {
        if (curr->client_fd == client_fd) {
            if (prev == NULL) {
                g_sessions = curr->next;
            } else {
                prev->next = curr->next;
            }
            pthread_mutex_unlock(&g_session_lock);
            free(curr);
            return;
        }
        prev = curr;
        curr = curr->next;
    }

    pthread_mutex_unlock(&g_session_lock);
}
/*
// pointer to pointer

void session_destroy(int client_fd)
{
    pthread_mutex_lock(&g_session_lock);

    session_t** curr = &g_sessions;

    while(*curr) {
        if ((*curr)->client_fd == client_fd) {
            session_t* session = *curr;
            *curr = session->next;
            pthread_mutex_unlock(&g_session_lock);

            free(session);
            return;
        }

        curr = &(*curr)->next;
    }

    pthread_mutex_unlock(&g_session_lock);
}
*/

void session_destroy_all(void)
{
    pthread_mutex_lock(&g_session_lock);

    session_t* session = g_sessions;
    g_sessions = NULL;

    pthread_mutex_unlock(&g_session_lock);

    while (session) {
        session_t* next = session->next;
        free(session);
        session = next;
    }
}
