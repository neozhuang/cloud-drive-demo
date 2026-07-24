#pragma once

#include <stdint.h>

#include "server/database_pool.h"
#include "server/session.h"
#include "server/thread_pool.h"
#include "server/timer_wheel.h"

void handle_accept_event(int epoll_fd, int listen_fd,
                         timer_wheel_t *timer_wheel);

void handle_client_event(int epoll_fd, int client_fd, uint32_t events,
                         thread_pool_t *thread_pool, database_pool_t* db_pool,
                          session_table_t *session_table,
                          timer_wheel_t *timer_wheel,
                          const char* storage_root, const char* transfer_temp_dir);
