#include "server/session.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void assert_initial_context(const session_context_t *context,
                                   uint64_t user_id,
                                   uint64_t root_path_id,
                                   const char *username)
{
    assert(context->user_id == user_id);
    assert(context->cwd_id == root_path_id);
    assert(strcmp(context->username, username) == 0);
    assert(strcmp(context->cwd, "/") == 0);
}

static void test_session_lifecycle(void)
{
    const int main_fd = 10;
    const int transfer_fd = 11;
    const uint64_t user_id = 42;
    const uint64_t root_path_id = 100;
    session_table_t *table = session_table_create();
    session_id_t session_id;
    session_context_t context;

    assert(table != NULL);
    assert(session_create(table, main_fd, user_id, "alice", root_path_id,
                          &session_id) == 0);
    assert(!session_id_is_empty(&session_id));

    assert(session_authorize(table, main_fd, &session_id, &context) == 0);
    assert_initial_context(&context, user_id, root_path_id, "alice");

    assert(session_authorize(table, transfer_fd, &session_id, &context) == 0);
    assert_initial_context(&context, user_id, root_path_id, "alice");

    assert(session_update_cwd(table, main_fd, &session_id, 101, "/docs") == 0);
    assert(session_authorize(table, transfer_fd, &session_id, &context) == 0);
    assert(context.cwd_id == 101);
    assert(strcmp(context.cwd, "/docs") == 0);

    session_detach_fd(table, transfer_fd);
    assert(session_authorize(table, main_fd, &session_id, &context) == 0);

    session_detach_fd(table, main_fd);
    assert(session_authorize(table, main_fd, &session_id, &context) == -1);

    /* Detach is idempotent and NULL destruction is allowed. */
    session_detach_fd(table, main_fd);
    session_table_destroy(table);
    session_table_destroy(NULL);
}

static void test_invalid_and_conflicting_bindings(void)
{
    session_table_t *table = session_table_create();
    session_id_t first_id;
    session_id_t second_id;
    session_id_t empty_id = {{0}};
    session_context_t context;

    assert(table != NULL);
    assert(session_create(table, 20, 1, "alice", 10, &first_id) == 0);

    /* One fd cannot be the initial connection of two sessions. */
    assert(session_create(table, 20, 2, "bob", 20, &second_id) == -1);
    assert(session_create(table, 21, 2, "bob", 20, &second_id) == 0);
    assert(!session_id_equal(&first_id, &second_id));

    assert(session_authorize(table, 22, &empty_id, &context) == -1);

    /* fd 20 is already bound to the first session. */
    assert(session_authorize(table, 20, &second_id, &context) == -1);

    session_detach_fd(table, 20);
    session_detach_fd(table, 21);
    session_table_destroy(table);
}

int main(void)
{
    test_session_lifecycle();
    test_invalid_and_conflicting_bindings();

    puts("session tests passed");
    return 0;
}
