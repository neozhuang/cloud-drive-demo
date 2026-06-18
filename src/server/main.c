#include <asm-generic/errno-base.h>
#include <linux/limits.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "common/protocol.h"
#include "server/config.h"
#include "common/utils.h"
#include "common/log.h"
#include "server/network.h"
#include "server/thread_pool.h"
#include "server/session.h"
#include "server/handler.h"
#include "server/auth_config.h"

#define MAX_EVENTS 1024

/*
 * pipe_fd:
 *   pipe_fd[0] -> read end-point
 *   pipe_fd[1] -> write end-point
 *
 * The SIGINT handler writes one byte to this pipe so the epoll loop can
 * shut down from normal process context instead of doing cleanup in a signal
 * handler.
 */
static int pipe_fd[2];

/*
 * handle_sigint:
 * SIGINT (Ctrl+C) notification entry.
 */
static void handle_sigint(int signo) {
    (void)signo;
    write(pipe_fd[1], "q", 1);
}

static void close_client(int epoll_fd, int client_fd);
static void handle_accept_event(int epoll_fd, int listen_fd);
static void handle_client_event(int epoll_fd, 
                                int client_fd, 
                                uint32_t events, 
                                thread_pool_t *thread_pool, 
                                const auth_config_t *auth_config,
                                const char *cloud_drive_root);
static int ensure_parent_dir(const char *path);

// Launch cdd-server from either cloud_drive_demo or client-drive-demo/bin.
int main(int argc, char* argv[]){
    char project_dir[PATH_MAX];     // project root dir, ../client-drive-demo/
    char config_path[PATH_MAX];     // config file absolute path 
    char log_file[PATH_MAX];        // log file absolute path 
    char users_file[PATH_MAX];      // accessible users file 
    char cloud_drive_root[PATH_MAX];// cloud drive root dir ../client-drive-demo/data/users/
    const char* path = NULL;
    int listen_fd;
    int epoll_fd;
    thread_pool_t* thread_pool;

    // Resolve paths relative to the project root so the binary can be started
    // from different working directories.
    if (get_project_dir(project_dir, sizeof(project_dir)) != 0) {
        return -1; 
    }

    /**
     * Handle command line args.
     *
     * If the user does not choose a config file, use the default config.
     */
    if (argc > 2) {
        fprintf(stderr, "usage: %s [config-file]\n", argv[0]);
        return 1;
    }
    if (argc == 2) {
        path = argv[1];
    } else {
        // get default config file absolute path
        if(join_path(config_path, sizeof(config_path), project_dir, "config/server.conf") != 0)
            return -1;
        path = config_path;
    }

    /**
      * Load server config.
      *
      * In this version, just use our own config parser. 
      * In future version that introduced mysql and some other complicated module,
      * use third_party parser to parse config file with section. 
      */ 
    server_config_t config;
    if(server_config_load(&config, path) != 0)
        return -1;
    server_config_print(&config);

    // Initialize logging before network startup so later failures are recorded.
    if (join_path(log_file, sizeof(log_file), project_dir, config.log_file) != 0)
        return -1;
    if (ensure_parent_dir(log_file) != 0) {
        fprintf(stderr, "failed to create log directory for %s\n", log_file);
        return -1;
    }
    // init log module
    if (log_init(config.log_level, log_file) != 0) {
        fprintf(stderr, "log_init failed, fallback to stdout\n");
    }
    // LOG_INFO("log module test");

    // Ensure the configured cloud drive root exists before accepting clients.
    if (join_path(cloud_drive_root, sizeof(cloud_drive_root), project_dir, config.root_dir) != 0) {
        return -1; 
    }
    printf("cloud drive root = %s\n", cloud_drive_root);
    if (mkdir_p(cloud_drive_root, 0755) != 0) {
        LOG_ERROR("failed to mkdir cloud drive root %s errno=%d", cloud_drive_root, errno);
        return -1;
    }

    // Load the user database used by authentication handlers.
    auth_config_t auth_config;
    if (join_path(users_file, sizeof(users_file), project_dir, "config/users.conf") != 0)
        return -1;
    // load accessible users file 
    if (auth_config_load(&auth_config, users_file) != 0) {
        LOG_ERROR("failed to load users file");
        return -1;
    }


    /**
     * Signal handling.
     *
     * - Ignore SIGPIPE; otherwise, writing to a disconnected client may kill
     *   the process by the default SIGPIPE action.
     * - Install SIGINT handler for graceful shutdown.
     */
    signal(SIGPIPE, SIG_IGN);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        perror("sigaction");
        return 1;
    }

    // Create the self-pipe used to wake epoll on SIGINT.
    if (pipe(pipe_fd) != 0) {
        perror("pipe");
        return 1;
    }

    // init network
    listen_fd = network_listen(config.host, config.port);
    if (listen_fd < 0) {
        return -1;
    }

    // init thread pool
    thread_pool = thread_pool_create(config.thread_num, config.queue_capacity);
    if (thread_pool == NULL) {
        LOG_DEBUG("thread_pool_create failed");
        return -1;
    }

    // Create epoll instance and watch both network and shutdown events.
    // listen_fd: new client connections
    // pipe_fd[0]: shutdown notifications from handle_sigint
    epoll_fd = epoll_create1(0);
    network_add_epoll_fd(epoll_fd, listen_fd, EPOLLIN);
    network_add_epoll_fd(epoll_fd, pipe_fd[0], EPOLLIN);

    struct epoll_event events[MAX_EVENTS];

    while (1) {
        int nready = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        if (nready == -1) {
            // interrupted by signal, just continue
            if (errno == EINTR) {
                continue;
            }
            // other error
            perror("epoll_wait");
            return -1;
        }

        for (int i = 0; i < nready; i++) {
            int ready_fd = events[i].data.fd;


            /**
             * If the ready fd is the pipe read end, the server received an
             * exit notification.
             */
            if (ready_fd == pipe_fd[0]) {
                char buf[16];
                // Drain the pipe so pipe_fd[0] no longer stays readable.
                read(pipe_fd[0], buf, sizeof(buf));
                LOG_INFO("server received shutdown signal");
                close(listen_fd);
                close(pipe_fd[0]);
                close(pipe_fd[1]);
                close(epoll_fd);
                thread_pool_destroy(thread_pool);
                LOG_INFO("server closed");
                log_close();
                return 0;
            }

            /**
             * The listening socket is readable when a new client is waiting
             * in the accept queue.
             */
            if (ready_fd == listen_fd) {
                handle_accept_event(epoll_fd, listen_fd);
            } else {
                /**
                 * For client fds, either a packet is ready to read or the
                 * connection state changed.
                 */
                handle_client_event(epoll_fd, ready_fd, events[i].events, thread_pool, &auth_config, cloud_drive_root);
            }
        }
    }

    session_destroy_all();
    thread_pool_destroy(thread_pool);
    log_close();
    return 0;
}

static void close_client(int epoll_fd, int client_fd)
{
    // Remove the client from all server-owned state before closing the fd.
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
    session_destroy(client_fd);
    close(client_fd);
}

static void handle_accept_event(int epoll_fd, int listen_fd)
{
    // Accept one pending connection and register its session with epoll.
    int client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd == -1) {
        LOG_WARN("accept failed errno=%d", errno);
        return;
    }

    if (session_create(client_fd) == NULL) {
        LOG_ERROR("failed to allocate client session");
        close(client_fd);
        return;
    }

    if (network_add_epoll_fd(epoll_fd, client_fd, EPOLLIN | EPOLLRDHUP) != 0) {
        LOG_ERROR("failed to add client fd to epoll listen queue");
        session_destroy(client_fd);
        close(client_fd);
        return;
    }

    LOG_INFO("accepted client fd=%d", client_fd);
}

static void handle_client_event(int epoll_fd, 
                                int client_fd, 
                                uint32_t events, 
                                thread_pool_t *thread_pool, 
                                const auth_config_t *auth_config,
                                const char *cloud_drive_root)
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
            LOG_ERROR("malloc packet task failed");
            close_client(epoll_fd, client_fd);
            return;
        }

        task->client_fd = client_fd;
        task->auth_config = auth_config;
        task->cloud_drive_root = cloud_drive_root;

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
            transfer_task->auth_config = task->auth_config;
            transfer_task->cloud_drive_root = task->cloud_drive_root;
            // Caution: cannot free_packet due to tranfer_task still use the field of packet.
            free(task); 

            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);

            if (thread_pool_add(thread_pool, handle_transfer_task, transfer_task) != 0) {
                free_packet(&transfer_task->packet);
                free(transfer_task);
                close_client(epoll_fd, client_fd);
            }
            return;
        }
        
        // handle common task: pwd, cd, ls, rmdir, mkdir, rm, ...
        if (thread_pool_add(thread_pool, handle_packet_task, task) != 0) {
            free_packet(&task->packet);
            free(task);
            LOG_ERROR("failed to add packet task");
            return;
        }
    }
}

static int ensure_parent_dir(const char *path)
{
    char dir[PATH_MAX];
    char *slash;

    if (path == NULL) {
        return -1;
    }

    if (snprintf(dir, sizeof(dir), "%s", path) >= (int)sizeof(dir)) {
        return -1;
    }

    // Create only the parent directory; the final path component is a file.
    slash = strrchr(dir, '/');
    if (slash == NULL) {
        return 0;
    }
    if (slash == dir) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }

    return mkdir_p(dir, 0755);
}
