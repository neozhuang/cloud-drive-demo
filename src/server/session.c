#include "server/session.h"

#include <limits.h>
#include <linux/limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdio.h>
#include <sys/resource.h>


struct session {
    pthread_mutex_t lock;

    int ref_count;
    int closing;

    int client_fd;
    int logged_in;
    uint64_t user_id;
    char username[64];
    uint64_t cwd_id;
    char cwd[PATH_MAX];
} ;

static session_t** sessions;
static size_t sessions_cap;
static pthread_mutex_t sessions_table_lock = PTHREAD_MUTEX_INITIALIZER;

int session_table_init(void)
{
    struct rlimit lim;

    if (getrlimit(RLIMIT_NOFILE, &lim) == -1) {
        perror("getrlimit");
        return -1;
    }

    if (lim.rlim_cur == RLIM_INFINITY) {
        sessions_cap = 1024 * 1024;
    } else {
        sessions_cap = lim.rlim_cur;
    }

    sessions = calloc(sessions_cap, sizeof(session_t*));
    if (sessions == NULL) {
        perror("calloc sessions");
        return -1;
    }

    return 0;
}

session_t *session_create(int client_fd)
{
    if (client_fd < 0 || (size_t)client_fd >= sessions_cap) {
        return NULL;
    }

    session_t *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        return NULL;
    }

    pthread_mutex_init(&s->lock, NULL);
    s->ref_count = 1;
    s->client_fd = client_fd;

    pthread_mutex_lock(&sessions_table_lock);

    if (sessions[client_fd] != NULL) {
        pthread_mutex_unlock(&sessions_table_lock);
        pthread_mutex_destroy(&s->lock);
        free(s);
        return NULL;
    }

    sessions[client_fd] = s;

    pthread_mutex_unlock(&sessions_table_lock);

    return s;
}

void session_destroy(int client_fd)
{
    session_t* s;
    if (client_fd < 0 || (size_t)client_fd >= sessions_cap) {
        return; 
    }

    pthread_mutex_lock(&sessions_table_lock);

    s = sessions[client_fd];
    if (s == NULL) {
        pthread_mutex_unlock(&sessions_table_lock);
        return;
    }

    sessions[client_fd] = NULL;

    pthread_mutex_lock(&s->lock);
    s->closing = 1;
    s->ref_count--;

    int should_free = s->ref_count == 0;

    pthread_mutex_unlock(&s->lock);
    pthread_mutex_unlock(&sessions_table_lock);

    if (should_free) {
        pthread_mutex_destroy(&s->lock);
        free(s);
    }
}


int session_table_destroy(void)
{
    pthread_mutex_lock(&sessions_table_lock);

    for (size_t i = 0; i < sessions_cap; i++) {

        if (sessions[i]) {
            pthread_mutex_destroy(&sessions[i]->lock);
            free(sessions[i]); 
            sessions[i] = NULL;
        }
    }

    free(sessions);
    sessions = NULL;
    sessions_cap = 0;

    pthread_mutex_unlock(&sessions_table_lock);
    pthread_mutex_destroy(&sessions_table_lock);

    return 0;
}

int session_set_login_state(int client_fd,
                            uint64_t user_id,
                            const char *username,
                            uint64_t root_path_id)
{
    session_t *s;

    if (username == NULL) {
        return -1;
    }

    if (client_fd < 0 || (size_t)client_fd >= sessions_cap) {
        return -1;
    }

    pthread_mutex_lock(&sessions_table_lock);

    s = sessions[client_fd];
    if (s == NULL) {
        pthread_mutex_unlock(&sessions_table_lock);
        return -1;
    }

    pthread_mutex_lock(&s->lock);
    pthread_mutex_unlock(&sessions_table_lock);

    if (s->closing) {
        pthread_mutex_unlock(&s->lock);
        return -1;
    }

    s->logged_in = 1;
    s->user_id = user_id;
    s->cwd_id = root_path_id;

    strncpy(s->username, username, sizeof(s->username) - 1);
    s->username[sizeof(s->username) - 1] = '\0';

    strncpy(s->cwd, "/", sizeof(s->cwd) - 1);
    s->cwd[sizeof(s->cwd) - 1] = '\0';

    pthread_mutex_unlock(&s->lock);

    return 0;
}

int session_get_username(int client_fd, char* username, int size)
{
    session_t *s;

    if (client_fd < 0 || (size_t)client_fd >= sessions_cap) {
        return -1;
    }

    pthread_mutex_lock(&sessions_table_lock);

    s = sessions[client_fd];
    if (s == NULL) {
        pthread_mutex_unlock(&sessions_table_lock);
        return -1;
    }

    pthread_mutex_lock(&s->lock);
    pthread_mutex_unlock(&sessions_table_lock);

    strncpy(username, s->username, size - 1);
    username[size - 1] = '\0';

    pthread_mutex_unlock(&s->lock);
    return 0;
}

int session_set_cwd(int client_fd, uint64_t cwd_id, const char *cwd)
{
    session_t *s;

    pthread_mutex_lock(&sessions_table_lock);

    s = sessions[client_fd];
    if (s == NULL) {
        pthread_mutex_unlock(&sessions_table_lock);
        return -1;
    }

    pthread_mutex_lock(&s->lock);
    pthread_mutex_unlock(&sessions_table_lock);

    s->cwd_id = cwd_id;
    strcpy(s->cwd, cwd);

    pthread_mutex_unlock(&s->lock);
    return 0;
}

int session_get_cwd(int client_fd, char *cwd, size_t size)
{
    session_t *s;

    if (cwd == NULL || size == 0) {
        return -1;
    }

    if (client_fd < 0 || (size_t)client_fd >= sessions_cap) {
        return -1;
    }

    pthread_mutex_lock(&sessions_table_lock);

    s = sessions[client_fd];
    if (s == NULL) {
        pthread_mutex_unlock(&sessions_table_lock);
        return -1;
    }

    pthread_mutex_lock(&s->lock);
    pthread_mutex_unlock(&sessions_table_lock);

    if (s->closing || !s->logged_in) {
        pthread_mutex_unlock(&s->lock);
        return -1;
    }

    strncpy(cwd, s->cwd, size - 1);
    cwd[size - 1] = '\0';

    pthread_mutex_unlock(&s->lock);
    return 0;
}

int session_get_location(int client_fd, uint64_t* user_id, uint64_t* cwd_id, char* cwd, int cwd_size)
{
    session_t *s;

    pthread_mutex_lock(&sessions_table_lock);

    s = sessions[client_fd];
    if (s == NULL) {
        pthread_mutex_unlock(&sessions_table_lock);
        return -1;
    }

    pthread_mutex_lock(&s->lock);
    pthread_mutex_unlock(&sessions_table_lock);

    if (s->closing || !s->logged_in) {
        pthread_mutex_unlock(&s->lock);
        return -1;
    }

    *user_id = s->user_id;
    *cwd_id = s->cwd_id;

    strncpy(cwd, s->cwd, cwd_size - 1);
    cwd[cwd_size - 1] = '\0';

    pthread_mutex_unlock(&s->lock);
    return 0;
}












#if 0
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


#endif

