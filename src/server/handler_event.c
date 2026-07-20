#include "server/handler_event.h"
#include "server/database_pool.h"
#include "server/handler_basic.h"
#include "server/handler_transfer.h"

#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <string.h>

#include "common/log.h"
#include "server/network.h"

static int command_is_anonymous(cmd_type_t type)
{
    return type == CMD_LOGIN_REQ || type == CMD_REGISTER_REQ;
}

static int send_event_error(int client_fd,
                            const session_id_t *session_id,
                            status_code_t status)
{
    packet_t response;

    if (packet_init(&response, CMD_ERROR, status, session_id, NULL, 0U) != 0) {
        return -1;
    }
    return protocol_send_packet(client_fd, &response);
}

static void close_client(int epoll_fd, int client_fd,
                         session_table_t *session_table)
{
    // Remove the client from all server-owned state before closing the fd.
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
    session_detach_fd(session_table, client_fd);
    close(client_fd);
}

void handle_accept_event(int epoll_fd, int listen_fd)
{
    // Accept one pending connection and register its session with epoll.
    int client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd == -1) {
        LOG_WARN("Failed to accept client connection");
        return;
    }

    if (network_add_epoll_fd(epoll_fd, client_fd, EPOLLIN | EPOLLRDHUP) != 0) {
        LOG_ERROR("Failed to add client fd to epoll listen queue");
        close_client(epoll_fd, client_fd, NULL);
        return;
    }
    LOG_INFO("Accepted client connection, fd=%d", client_fd);
}

void handle_client_event(int epoll_fd, int client_fd, uint32_t events, 
                         thread_pool_t *thread_pool, database_pool_t* db_pool,
                         session_table_t *session_table,
                         const char* storage_root, const char* transfer_temp_dir)
{
    // Remote hangup or socket error: release the client immediately.
    if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        close_client(epoll_fd, client_fd, session_table);
        return;
    }

    if (events & EPOLLIN) {
        // Packet handling may be expensive, so move it to the worker pool.
        packet_task_t *task = calloc(1, sizeof(*task));
        if (task == NULL) {
            close_client(epoll_fd, client_fd, session_table);
            return;
        }

        task->client_fd = client_fd;
        task->db_pool = db_pool;
        task->session_table = session_table;
        strncpy(task->storage_root, storage_root, sizeof(task->storage_root) - 1);
        task->storage_root[sizeof(task->storage_root) - 1] = '\0';

        if (protocol_recv_packet(client_fd, &task->packet) != 0) {
            free(task);
            close_client(epoll_fd, client_fd, session_table);
            return;
        }

        if (command_is_anonymous(task->packet.header.type)) {
            if (!session_id_is_empty(&task->packet.header.session_id)) {
                int send_ret = send_event_error(client_fd,
                                                &task->packet.header.session_id,
                                                STATUS_BAD_REQUEST);
                packet_release(&task->packet);
                free(task);
                if (send_ret != 0) {
                    close_client(epoll_fd, client_fd, session_table);
                }
                return;
            }
        } else if (session_authorize(session_table,
                                     client_fd,
                                     &task->packet.header.session_id,
                                     &task->session) != 0) {
            int send_ret = send_event_error(client_fd,
                                            &task->packet.header.session_id,
                                            STATUS_UNAUTHORIZED);
            packet_release(&task->packet);
            free(task);
            if (send_ret != 0) {
                close_client(epoll_fd, client_fd, session_table);
            }
            return;
        }

        // handle puts and gets request
        if (task->packet.header.type == CMD_PUTS_REQ || task->packet.header.type == CMD_GETS_REQ) {
            transfer_task_t *transfer_task = calloc(1, sizeof(*transfer_task));
            if (transfer_task == NULL) {
                packet_release(&task->packet);
                free(task);
                close_client(epoll_fd, client_fd, session_table);
                return;
            }

            transfer_task->epoll_fd = epoll_fd;
            transfer_task->client_fd = task->client_fd;
            transfer_task->packet = task->packet;
            transfer_task->db_pool = task->db_pool;
            transfer_task->session_table = task->session_table;
            transfer_task->session = task->session;
            strncpy(transfer_task->storage_root, storage_root, strlen(storage_root));
            transfer_task->storage_root[strlen(storage_root)] = '\0';
            strncpy(transfer_task->transfer_temp_dir, transfer_temp_dir, strlen(transfer_temp_dir));
            transfer_task->transfer_temp_dir[strlen(transfer_temp_dir)] = '\0';
            // Caution: cannot packet_release due to tranfer_task now take over packet instead of task.
            free(task); 

            // Delete the client_fd from the epoll listen queue temporarily 
            // as the client fd can send file data directly to the fixed server subthread successively.
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);

            // Handle the puts and gets task
            if (thread_pool_add(thread_pool, handle_transfer_task, transfer_task) != 0) {
                packet_release(&transfer_task->packet);
                free(transfer_task);
                close_client(epoll_fd, client_fd, session_table);
                LOG_ERROR("Failed to add gets/puts packet task");
            }
            return;
        }

        // Handle common task: login, register, pwd, cd, ls, rmdir, mkdir, rm, ...
        if (thread_pool_add(thread_pool, handle_basic_task, task) != 0) {
            packet_release(&task->packet);
            free(task);
            close_client(epoll_fd, client_fd, session_table);
            LOG_ERROR("Failed to add common packet task");
            return;
        }
    }
}
