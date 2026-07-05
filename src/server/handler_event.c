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
#include "server/session.h"


static void close_client(int epoll_fd, int client_fd)
{
    // Remove the client from all server-owned state before closing the fd.
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
    session_destroy(client_fd);
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

    if (session_create(client_fd) == NULL) {
        LOG_ERROR("Failed to allocate client session");
        close_client(epoll_fd, client_fd);
        return;
    }

    if (network_add_epoll_fd(epoll_fd, client_fd, EPOLLIN | EPOLLRDHUP) != 0) {
        LOG_ERROR("Failed to add client fd to epoll listen queue");
        close_client(epoll_fd, client_fd);
        return;
    }
    LOG_INFO("Accepted client connection, fd=%d", client_fd);
}

void handle_client_event(int epoll_fd, int client_fd, uint32_t events, 
                         thread_pool_t *thread_pool, database_pool_t* db_pool,
                         const char* storage_root, const char* transfer_temp_dir)
{
    // Remote hangup or socket error: release the client immediately.
    if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        close_client(epoll_fd, client_fd);
        return;
    }

    if (events & EPOLLIN) {
        // Packet handling may be expensive, so move it to the worker pool.
        packet_task_t *task = malloc(sizeof(packet_task_t));
        if (task == NULL) {
            close_client(epoll_fd, client_fd);
            return;
        }

        task->client_fd = client_fd;
        task->db_pool = db_pool;
        strncpy(task->storage_root, storage_root, sizeof(task->storage_root) - 1);
        task->storage_root[sizeof(task->storage_root) - 1] = '\0';

        if (recv_packet(client_fd, &task->packet) != 0) {
            free(task);
            close_client(epoll_fd, client_fd);
            return;
        }

        // handle puts and gets request
        if (task->packet.header.cmd_type == CMD_PUTS_REQ || task->packet.header.cmd_type == CMD_GETS_REQ) {
            transfer_task_t *transfer_task = malloc(sizeof(transfer_task_t));
            if (transfer_task == NULL) {
                free_packet(&task->packet);
                free(task);
                close_client(epoll_fd, client_fd);
                return;
            }

            transfer_task->epoll_fd = epoll_fd;
            transfer_task->client_fd = task->client_fd;
            transfer_task->packet = task->packet;
            transfer_task->db_pool = task->db_pool;
            strncpy(transfer_task->storage_root, storage_root, strlen(storage_root));
            transfer_task->storage_root[strlen(storage_root)] = '\0';
            strncpy(transfer_task->transfer_temp_dir, transfer_temp_dir, strlen(transfer_temp_dir));
            transfer_task->transfer_temp_dir[strlen(transfer_temp_dir)] = '\0';
            // Caution: cannot free_packet due to tranfer_task now take over packet instead of task.
            free(task); 

            // Delete the client_fd from the epoll listen queue temporarily 
            // as the client fd can send file data directly to the fixed server subthread successively.
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);

            // Handle the puts and gets task
            if (thread_pool_add(thread_pool, handle_transfer_task, transfer_task) != 0) {
                free_packet(&transfer_task->packet);
                free(transfer_task);
                close_client(epoll_fd, client_fd);
                LOG_ERROR("Failed to add gets/puts packet task");
            }
            return;
        }

        // Handle common task: login, register, pwd, cd, ls, rmdir, mkdir, rm, ...
        if (thread_pool_add(thread_pool, handle_basic_task, task) != 0) {
            free_packet(&task->packet);
            free(task);
            close_client(epoll_fd, client_fd);
            LOG_ERROR("Failed to add common packet task");
            return;
        }
    }
}

