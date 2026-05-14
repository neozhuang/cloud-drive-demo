#define _POSIX_C_SOURCE 200809L

#include "app/process_manager.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <unistd.h>

#include "infra/db/db_connection.h"
#include "transport/tcp_server.h"
#include "infra/thread_pool/thread_pool.h"

static int g_shutdown_write_fd = -1;

static void handle_sigusr1(int signo)
{
    const char exit_signal = 'q';
    ssize_t ignored;

    (void)signo;

    if (g_shutdown_write_fd < 0) {
        return;
    }

    ignored = write(g_shutdown_write_fd, &exit_signal, sizeof(exit_signal));
    (void)ignored;
}

static int register_parent_signal_handler(void)
{
    struct sigaction action;

    action.sa_handler = handle_sigusr1;
    action.sa_flags = 0;
    sigemptyset(&action.sa_mask);

    return sigaction(SIGUSR1, &action, NULL);
}

int server_run_parent_process(pid_t child_pid, int shutdown_write_fd)
{
    int child_status;

    g_shutdown_write_fd = shutdown_write_fd;
    if (register_parent_signal_handler() != 0) {
        perror("Failed to register SIGUSR1 handler");
        close(shutdown_write_fd);
        g_shutdown_write_fd = -1;
        return 1;
    }

    printf("Parent process %ld started network process %ld.\n", (long)getpid(),
           (long)child_pid);
    printf("Send SIGUSR1 to the parent to stop the network process.\n");
    fflush(stdout);

    while (waitpid(child_pid, &child_status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }

        perror("Failed to wait for child process");
        close(shutdown_write_fd);
        g_shutdown_write_fd = -1;
        return 1;
    }

    close(shutdown_write_fd);
    g_shutdown_write_fd = -1;

    if (WIFEXITED(child_status)) {
        return WEXITSTATUS(child_status);
    }

    if (WIFSIGNALED(child_status)) {
        fprintf(stderr, "Network process terminated by signal: %d\n",
                WTERMSIG(child_status));
    }

    return 1;
}

static int init_network_process(const ServerConfig *config, int shutdown_read_fd,
                                int *listen_fd, int *epoll_fd,
                                ThreadPool *thread_pool)
{
    if (db_library_init() != 0) {
        return -1;
    }

    if (db_ensure_schema(&config->mysql) != 0) {
        db_library_end();
        return -1;
    }

    *listen_fd = net_create_listen_socket(&config->listen);
    if (*listen_fd < 0) {
        db_library_end();
        return -1;
    }

    *epoll_fd = epoll_create1(0);
    if (*epoll_fd < 0) {
        perror("Failed to create epoll instance");
        close(*listen_fd);
        db_library_end();
        return -1;
    }

    if (net_add_epoll_fd(*epoll_fd, *listen_fd, EPOLLIN) != 0 ||
        net_add_epoll_fd(*epoll_fd, shutdown_read_fd, EPOLLIN) != 0) {
        perror("Failed to add fd to epoll");
        close(*epoll_fd);
        close(*listen_fd);
        db_library_end();
        return -1;
    }

    if (thread_pool_init(thread_pool, config->thread_pool.worker_threads,
                         config->thread_pool.queue_capacity,
                         &config->mysql) != 0) {
        perror("Failed to initialize thread pool");
        close(*epoll_fd);
        close(*listen_fd);
        db_library_end();
        return -1;
    }

    return 0;
}

static void print_network_process_started(const ServerConfig *config)
{
    printf("Listening on %s:%d with epoll.\n", config->listen.host,
           config->listen.port);
    printf("Thread pool initialized with %d workers and queue capacity %d.\n",
           config->thread_pool.worker_threads,
           config->thread_pool.queue_capacity);
    printf("Each worker thread owns one MySQL connection.\n");
    fflush(stdout);
}

static int handle_epoll_event(int epoll_fd, int listen_fd, int shutdown_read_fd,
                              const struct epoll_event *event,
                              ThreadPool *thread_pool)
{
    if (event->data.fd == shutdown_read_fd) {
        if (net_drain_shutdown_fd(shutdown_read_fd) != 0) {
            perror("Failed to read shutdown pipe");
        }
        return 1;
    }

    if (event->data.fd == listen_fd) {
        net_accept_pending_clients(epoll_fd, listen_fd);
        return 0;
    }

    if ((event->events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0) {
        net_close_client(epoll_fd, event->data.fd);
    } else if ((event->events & EPOLLIN) != 0) {
        net_handle_client_packets(epoll_fd, event->data.fd, thread_pool);
    }

    return 0;
}

static int run_event_loop(int epoll_fd, int listen_fd, int shutdown_read_fd,
                          ThreadPool *thread_pool)
{
    int should_stop = 0;
    struct epoll_event events[NET_MAX_EVENTS];

    while (!should_stop) {
        int event_count = epoll_wait(epoll_fd, events, NET_MAX_EVENTS, -1);
        int index;

        if (event_count < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("epoll_wait failed");
            return -1;
        }

        for (index = 0; index < event_count; ++index) {
            should_stop = handle_epoll_event(epoll_fd, listen_fd,
                                             shutdown_read_fd, &events[index],
                                             thread_pool);
            if (should_stop) {
                break;
            }
        }
    }

    return 0;
}

int server_run_network_process(const ServerConfig *config, int shutdown_read_fd)
{
    int listen_fd;
    int epoll_fd;
    int result;
    ThreadPool thread_pool;

    if (init_network_process(config, shutdown_read_fd, &listen_fd, &epoll_fd,
                             &thread_pool) != 0) {
        close(shutdown_read_fd);
        return 1;
    }

    print_network_process_started(config);
    result = run_event_loop(epoll_fd, listen_fd, shutdown_read_fd, &thread_pool);

    printf("Network server stopped.\n");
    thread_pool_destroy(&thread_pool);
    db_library_end();
    close(epoll_fd);
    close(listen_fd);
    close(shutdown_read_fd);
    return result == 0 ? 0 : 1;
}
