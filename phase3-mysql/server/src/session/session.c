#include "session/session.h"

#include <errno.h>
#include <stdlib.h>

static Session *g_sessions = NULL;

Session *session_create(int client_fd)
{
    Session *session = (Session *)calloc(1, sizeof(Session));

    if (session == NULL) {
        return NULL;
    }

    session->client_fd = client_fd;
    session->user_id = 0;

    session->next = g_sessions;
    g_sessions = session;
    return session;
}

Session *session_find(int client_fd)
{
    Session *session = g_sessions;

    while (session != NULL) {
        if (session->client_fd == client_fd) {
            return session;
        }
        session = session->next;
    }

    return NULL;
}

void session_destroy(int client_fd)
{
    Session **cursor = &g_sessions;

    while (*cursor != NULL) {
        Session *session = *cursor;

        if (session->client_fd == client_fd) {
            *cursor = session->next;
            free(session);
            return;
        }

        cursor = &session->next;
    }
}

void session_destroy_all(void)
{
    while (g_sessions != NULL) {
        Session *session = g_sessions;

        g_sessions = session->next;
        free(session);
    }
}

int session_set_user(Session *session, uint64_t user_id)
{
    if (session == NULL) {
        errno = EINVAL;
        return -1;
    }

    session->user_id = user_id;
    return 0;
}
