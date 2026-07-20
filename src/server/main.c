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
#include "server/database.h"
#include "server/database_pool.h"
#include "server/thread_pool.h"
#include "server/session.h"
#include "server/handler_event.h"
#include "common/tui.h"

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


// Launch cdd-server from either cloud-drive-demo or client-drive-demo/bin.
int main(int argc, char* argv[]){
    char project_dir[PATH_MAX];     // project root dir, client-drive-demo
    char config_path[PATH_MAX];     // config file absolute path 
    char log_file[PATH_MAX];        // log file absolute path 
    char storage_root[PATH_MAX];    // server storage root
    char transfer_temp_dir[PATH_MAX]; // temp file path

    int listen_fd;
    int epoll_fd;

    thread_pool_t* thread_pool;
    database_pool_t* db_pool;
    session_table_t* session_table;

    /* Print a beautiful banner and the current time */
    tui_print_banner();
    tui_print_time("Cloud Drive Demo Server");


    // Resolve paths relative to the project root so the binary can be started
    // from different working directories.
    if (get_project_dir(project_dir, sizeof(project_dir)) != 0) {
        return -1; 
    }

     // If the user does not choose a config file, use the default config.
    if (argc > 2) {
        fprintf(stderr, "usage: %s [config-file]\n", argv[0]);
        return 1;
    }
    if (argc == 2) {
        strcpy(config_path, argv[1]);
    } else {
        // Get default config file absolute path
        if(join_path(config_path, sizeof(config_path), project_dir, "config/server.conf") != 0)
            return -1;
    }

    // Load server config.
    server_config_t config;
    memset(&config, 0, sizeof(config));
    if(server_config_load(&config, config_path) != 0)
        return -1;
    server_config_print(&config);

    // Initialize logging before network startup so later failures are recorded.
    if (join_path(log_file, sizeof(log_file), project_dir, config.log.log_file) != 0)
        return -1;
    if (ensure_parent_dir(log_file) != 0) {
        return -1;
    }
    if (log_init(config.log.log_level, log_file) != 0) {
        fprintf(stderr, "log_init failed, fallback to stdout\n");
    }

    // Ensure the configured cloud drive root exists before accepting clients.
    if (join_path(storage_root, sizeof(storage_root), project_dir, config.storage.root_dir) != 0 
        || join_path(transfer_temp_dir, sizeof(transfer_temp_dir), project_dir, config.storage.transfer_temp_dir) != 0) {
        return -1; 
    }
    if (mkdir_p(storage_root, 0755) != 0 || mkdir_p(transfer_temp_dir, 0755) != 0) {
        return -1;
    }

    LOG_INFO("server config loading completed, address = %s, port = %s", 
             config.network.host, config.network.port);


    // Ignore the SIGPIPE to avoid the disconnected client killing the entire server  
    signal(SIGPIPE, SIG_IGN);
    // Install SIGINT handler for graceful shutdown.
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        perror("sigaction");
        return 1;
    }

    // Initialize mysql database and tables
    // if not exist, then create them.
    mysql_config_t mysql_config = config.mysql;
    if (database_init(mysql_config.host, mysql_config.user, mysql_config.password, 
                      mysql_config.database, atoi(mysql_config.port), mysql_config.charset) != 0) {
        LOG_ERROR("Failed to initialize database, server refuse to launch"); 
        log_close();
        return -1;
    }

    // Initialize mysql connection pool before worker threads can handle requests.
    db_pool = database_pool_create(&config.mysql);
    if (db_pool == NULL) {
        log_close();
        return -1;
    }

    // Initialize thread pool
    thread_pool = thread_pool_create(config.thread_pool.thread_num, config.thread_pool.queue_capacity);
    if (thread_pool == NULL) {
        LOG_ERROR("Failed to initialize thread pool");
        database_pool_destroy(db_pool);
        log_close();
        return -1;
    }

    // Initialize client session table
    if ((session_table = session_table_create()) == NULL) {
        LOG_ERROR("Failed to initialize session table");
        thread_pool_destroy(thread_pool);
        database_pool_destroy(db_pool);
        log_close();
        return -1;
    }

    // Create the self-pipe used to wake epoll on SIGINT.
    if (pipe(pipe_fd) != 0) {
        perror("pipe");
        thread_pool_destroy(thread_pool);
        session_table_destroy(session_table);
        database_pool_destroy(db_pool);
        log_close();
        return -1;
    }

    // Initialize network after dependencies are ready, so accepted clients are serviceable.
    listen_fd = network_listen(config.network.host, config.network.port);
    if (listen_fd < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        thread_pool_destroy(thread_pool);
        session_table_destroy(session_table);
        database_pool_destroy(db_pool);
        log_close();
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
            close(listen_fd);
            close(pipe_fd[0]);
            close(pipe_fd[1]);
            close(epoll_fd);
            thread_pool_destroy(thread_pool);
            session_table_destroy(session_table);
            database_pool_destroy(db_pool);
            log_close();
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
                session_table_destroy(session_table);
                database_pool_destroy(db_pool);
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
                handle_client_event(epoll_fd, ready_fd, events[i].events,
                                    thread_pool, db_pool, session_table,
                                    storage_root, transfer_temp_dir);
            }
        }
    }

    close(listen_fd);
    close(pipe_fd[0]);
    close(pipe_fd[1]);
    close(epoll_fd);
    thread_pool_destroy(thread_pool);
    session_table_destroy(session_table);
    database_pool_destroy(db_pool);
    LOG_INFO("server closed");
    log_close();
    return 0;
}
