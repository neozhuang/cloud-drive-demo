#include "server/session.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>

#define SESSION_BUCKET_COUNT 1024U
#define SESSION_ID_GENERATION_ATTEMPTS 8

typedef struct session session_t;
typedef struct connection_binding connection_binding_t;

struct connection_binding {
    int client_fd;
    session_t *session;
    connection_binding_t *fd_next;
    connection_binding_t *session_next;
};

struct session {
    session_id_t id;
    uint64_t user_id;
    uint64_t cwd_id;
    char username[64];
    char cwd[PATH_MAX];
    struct timespec created_at;
    struct timespec last_active;
    size_t connection_count;
    connection_binding_t *connections;
    session_t *hash_next;
};

struct session_table {
    session_t **session_buckets;
    connection_binding_t **fd_buckets;
    size_t bucket_count;
    size_t session_count;
    pthread_mutex_t lock;
};

static size_t hash_session_id(const session_id_t *session_id,
                              size_t bucket_count)
{
    uint64_t hash = UINT64_C(1469598103934665603);

    for (size_t i = 0; i < SESSION_ID_SIZE; ++i) {
        hash ^= session_id->bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return (size_t)(hash % bucket_count);
}

static size_t hash_fd(int client_fd, size_t bucket_count)
{
    return (size_t)(unsigned int)client_fd % bucket_count;
}

static int generate_session_id(session_id_t *session_id)
{
    size_t offset = 0;

    if (session_id == NULL) {
        return -1;
    }

    while (offset < SESSION_ID_SIZE) {
        ssize_t ret = getrandom(session_id->bytes + offset,
                                SESSION_ID_SIZE - offset,
                                0);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (ret == 0) {
            return -1;
        }
        offset += (size_t)ret;
    }

    return session_id_is_empty(session_id) ? -1 : 0;
}

static session_t *find_session_locked(session_table_t *table,
                                      const session_id_t *session_id)
{
    size_t index = hash_session_id(session_id, table->bucket_count);
    session_t *session = table->session_buckets[index];

    while (session != NULL) {
        if (session_id_equal(&session->id, session_id)) {
            return session;
        }
        session = session->hash_next;
    }
    return NULL;
}

static connection_binding_t *find_binding_locked(session_table_t *table,
                                                 int client_fd)
{
    size_t index = hash_fd(client_fd, table->bucket_count);
    connection_binding_t *binding = table->fd_buckets[index];

    while (binding != NULL) {
        if (binding->client_fd == client_fd) {
            return binding;
        }
        binding = binding->fd_next;
    }
    return NULL;
}

static void copy_context(const session_t *session,
                         session_context_t *context)
{
    memset(context, 0, sizeof(*context));
    context->user_id = session->user_id;
    context->cwd_id = session->cwd_id;
    strncpy(context->username, session->username,
            sizeof(context->username) - 1U);
    strncpy(context->cwd, session->cwd, sizeof(context->cwd) - 1U);
}

session_table_t *session_table_create(void)
{
    session_table_t *table = calloc(1, sizeof(*table));

    if (table == NULL) {
        return NULL;
    }

    table->bucket_count = SESSION_BUCKET_COUNT;
    table->session_buckets = calloc(table->bucket_count,
                                    sizeof(*table->session_buckets));
    table->fd_buckets = calloc(table->bucket_count,
                               sizeof(*table->fd_buckets));
    if (table->session_buckets == NULL || table->fd_buckets == NULL) {
        free(table->fd_buckets);
        free(table->session_buckets);
        free(table);
        return NULL;
    }

    if (pthread_mutex_init(&table->lock, NULL) != 0) {
        free(table->fd_buckets);
        free(table->session_buckets);
        free(table);
        return NULL;
    }
    return table;
}

void session_table_destroy(session_table_t *table)
{
    if (table == NULL) {
        return;
    }

    for (size_t i = 0; i < table->bucket_count; ++i) {
        session_t *session = table->session_buckets[i];

        while (session != NULL) {
            session_t *next_session = session->hash_next;
            connection_binding_t *binding = session->connections;

            while (binding != NULL) {
                connection_binding_t *next_binding = binding->session_next;
                free(binding);
                binding = next_binding;
            }
            free(session);
            session = next_session;
        }
    }

    pthread_mutex_destroy(&table->lock);
    free(table->fd_buckets);
    free(table->session_buckets);
    free(table);
}

int session_create(session_table_t *table,
                   int client_fd,
                   uint64_t user_id,
                   const char *username,
                   uint64_t root_path_id,
                   session_id_t *out_session_id)
{
    session_t *session;
    connection_binding_t *binding;
    struct timespec now;

    if (table == NULL || client_fd < 0 || username == NULL ||
        username[0] == '\0' || strlen(username) >= sizeof(session->username) ||
        out_session_id == NULL) {
        return -1;
    }

    session = calloc(1, sizeof(*session));
    binding = calloc(1, sizeof(*binding));
    if (session == NULL || binding == NULL) {
        free(binding);
        free(session);
        return -1;
    }

    for (int attempt = 0; attempt < SESSION_ID_GENERATION_ATTEMPTS; ++attempt) {
        if (generate_session_id(&session->id) != 0) {
            break;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            break;
        }

        pthread_mutex_lock(&table->lock);

        if (find_binding_locked(table, client_fd) != NULL) {
            pthread_mutex_unlock(&table->lock);
            free(binding);
            free(session);
            return -1;
        }
        if (find_session_locked(table, &session->id) != NULL) {
            pthread_mutex_unlock(&table->lock);
            continue;
        }

        session->user_id = user_id;
        session->cwd_id = root_path_id;
        strncpy(session->username, username, sizeof(session->username) - 1U);
        strcpy(session->cwd, "/");
        session->created_at = now;
        session->last_active = session->created_at;

        binding->client_fd = client_fd;
        binding->session = session;

        size_t fd_index = hash_fd(client_fd, table->bucket_count);
        binding->fd_next = table->fd_buckets[fd_index];
        table->fd_buckets[fd_index] = binding;

        binding->session_next = session->connections;
        session->connections = binding;
        session->connection_count = 1U;

        size_t session_index = hash_session_id(&session->id,
                                               table->bucket_count);
        session->hash_next = table->session_buckets[session_index];
        table->session_buckets[session_index] = session;
        table->session_count++;
        *out_session_id = session->id;

        pthread_mutex_unlock(&table->lock);
        return 0;
    }

    free(binding);
    free(session);
    return -1;
}

int session_authorize(session_table_t *table,
                      int client_fd,
                      const session_id_t *session_id,
                      session_context_t *context)
{
    session_t *session;
    connection_binding_t *binding;
    struct timespec now;

    if (table == NULL || client_fd < 0 || session_id == NULL ||
        session_id_is_empty(session_id) || context == NULL) {
        return -1;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return -1;
    }

    pthread_mutex_lock(&table->lock);
    session = find_session_locked(table, session_id);
    if (session == NULL) {
        pthread_mutex_unlock(&table->lock);
        return -1;
    }

    binding = find_binding_locked(table, client_fd);
    if (binding != NULL && binding->session != session) {
        pthread_mutex_unlock(&table->lock);
        return -1;
    }

    if (binding == NULL) {
        size_t fd_index;

        binding = calloc(1, sizeof(*binding));
        if (binding == NULL) {
            pthread_mutex_unlock(&table->lock);
            return -1;
        }

        binding->client_fd = client_fd;
        binding->session = session;
        fd_index = hash_fd(client_fd, table->bucket_count);
        binding->fd_next = table->fd_buckets[fd_index];
        table->fd_buckets[fd_index] = binding;
        binding->session_next = session->connections;
        session->connections = binding;
        session->connection_count++;
    }

    session->last_active = now;
    copy_context(session, context);
    pthread_mutex_unlock(&table->lock);
    return 0;
}

int session_update_cwd(session_table_t *table,
                       int client_fd,
                       const session_id_t *session_id,
                       uint64_t cwd_id,
                       const char *cwd)
{
    session_t *session;
    connection_binding_t *binding;
    struct timespec now;

    if (table == NULL || client_fd < 0 || session_id == NULL ||
        session_id_is_empty(session_id) || cwd == NULL || cwd[0] != '/' ||
        strlen(cwd) >= PATH_MAX) {
        return -1;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return -1;
    }

    pthread_mutex_lock(&table->lock);
    session = find_session_locked(table, session_id);
    binding = find_binding_locked(table, client_fd);
    if (session == NULL || binding == NULL || binding->session != session) {
        pthread_mutex_unlock(&table->lock);
        return -1;
    }

    session->cwd_id = cwd_id;
    strncpy(session->cwd, cwd, sizeof(session->cwd) - 1U);
    session->cwd[sizeof(session->cwd) - 1U] = '\0';
    session->last_active = now;

    pthread_mutex_unlock(&table->lock);
    return 0;
}

void session_detach_fd(session_table_t *table, int client_fd)
{
    connection_binding_t **fd_link;
    connection_binding_t **session_link;
    connection_binding_t *binding;
    session_t *session;
    size_t fd_index;

    if (table == NULL || client_fd < 0) {
        return;
    }

    pthread_mutex_lock(&table->lock);
    fd_index = hash_fd(client_fd, table->bucket_count);
    fd_link = &table->fd_buckets[fd_index];
    while (*fd_link != NULL && (*fd_link)->client_fd != client_fd) {
        fd_link = &(*fd_link)->fd_next;
    }
    if (*fd_link == NULL) {
        pthread_mutex_unlock(&table->lock);
        return;
    }

    binding = *fd_link;
    *fd_link = binding->fd_next;
    session = binding->session;

    session_link = &session->connections;
    while (*session_link != NULL && *session_link != binding) {
        session_link = &(*session_link)->session_next;
    }
    if (*session_link == binding) {
        *session_link = binding->session_next;
    }
    session->connection_count--;
    free(binding);

    if (session->connection_count == 0U) {
        size_t session_index = hash_session_id(&session->id,
                                               table->bucket_count);
        session_t **session_hash_link =
            &table->session_buckets[session_index];

        while (*session_hash_link != NULL && *session_hash_link != session) {
            session_hash_link = &(*session_hash_link)->hash_next;
        }
        if (*session_hash_link == session) {
            *session_hash_link = session->hash_next;
        }
        table->session_count--;
        free(session);
    }

    pthread_mutex_unlock(&table->lock);
}
