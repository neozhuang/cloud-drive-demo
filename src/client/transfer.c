#include "client/transfer.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/evp.h>

#include "client/connection.h"
#include "client/runtime.h"
#include "common/log.h"
#include "common/protocol.h"
#include "common/utils.h"

typedef enum {
    TRANSFER_UPLOAD,
    TRANSFER_DOWNLOAD
} transfer_kind_t;

typedef enum {
    TRANSFER_RESULT_OK,
    TRANSFER_RESULT_FAILED,
    TRANSFER_RESULT_CANCELED
} transfer_result_t;

typedef struct transfer_event {
    transfer_kind_t kind;
    transfer_result_t result;
    char path[PATH_MAX];
    struct transfer_event *next;
} transfer_event_t;

typedef struct transfer_task {
    struct transfer_manager *manager;
    struct transfer_task *next;
    transfer_event_t *event;
    transfer_kind_t kind;
    atomic_bool canceled;
    int socket_fd;

    session_id_t session_id;
    char host[sizeof(((remote_config_t *)0)->host)];
    char port[sizeof(((remote_config_t *)0)->port)];
    int connect_timeout_ms;
    int io_timeout_ms;

    char local_path[PATH_MAX];
    char remote_path[PATH_MAX];
    char target_path[PATH_MAX];
    char part_path[PATH_MAX];
} transfer_task_t;

struct transfer_manager {
    pthread_mutex_t lock;
    pthread_cond_t idle;
    transfer_task_t *active;
    size_t active_count;
    size_t max_concurrent;
    int stopping;
    transfer_event_t *events_head;
    transfer_event_t *events_tail;
};

static int copy_string(char *dst, size_t dst_size, const char *src)
{
    size_t length = strlen(src);

    if (length >= dst_size) {
        return -1;
    }
    memcpy(dst, src, length + 1);
    return 0;
}

static int task_is_canceled(const transfer_task_t *task)
{
    return atomic_load_explicit(&task->canceled, memory_order_acquire);
}

static int task_send(transfer_task_t *task, cmd_type_t type,
                     status_code_t status, const void *payload,
                     uint32_t payload_len)
{
    packet_t packet;

    if (task_is_canceled(task) ||
        packet_init(&packet, type, status, &task->session_id,
                    payload, payload_len) != 0) {
        return -1;
    }
    return protocol_send_packet(task->socket_fd, &packet);
}

static int task_recv(transfer_task_t *task, packet_t *packet)
{
    if (task_is_canceled(task) ||
        protocol_recv_packet(task->socket_fd, packet) != 0) {
        return -1;
    }
    if (!session_id_equal(&packet->header.session_id, &task->session_id)) {
        packet_release(packet);
        (void)task_send(task, CMD_ERROR, STATUS_UNAUTHORIZED, NULL, 0);
        return -1;
    }
    return 0;
}

static void task_report_error(transfer_task_t *task, status_code_t status)
{
    if (!task_is_canceled(task) && task->socket_fd >= 0) {
        (void)task_send(task, CMD_ERROR, status, NULL, 0);
    }
}

static int task_compute_sha256(transfer_task_t *task, int fd, char output[65])
{
    EVP_MD_CTX *context;
    unsigned char buffer[64U * 1024U];
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_length = 0;
    int result = -1;

    if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
        return -1;
    }
    context = EVP_MD_CTX_new();
    if (context == NULL || EVP_DigestInit_ex(context, EVP_sha256(), NULL) != 1) {
        EVP_MD_CTX_free(context);
        return -1;
    }

    for (;;) {
        ssize_t count;

        if (task_is_canceled(task)) {
            goto done;
        }
        count = read(fd, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 ||
            (count > 0 && EVP_DigestUpdate(context, buffer, (size_t)count) != 1)) {
            goto done;
        }
        if (count == 0) {
            break;
        }
    }

    if (EVP_DigestFinal_ex(context, digest, &digest_length) != 1 ||
        digest_length != 32U) {
        goto done;
    }
    for (unsigned int i = 0; i < digest_length; ++i) {
        snprintf(output + i * 2U, 3U, "%02x", digest[i]);
    }
    output[64] = '\0';
    result = 0;

done:
    EVP_MD_CTX_free(context);
    return result;
}

static int task_publish_socket(transfer_task_t *task, int fd)
{
    transfer_manager_t *manager = task->manager;

    pthread_mutex_lock(&manager->lock);
    if (task_is_canceled(task)) {
        pthread_mutex_unlock(&manager->lock);
        return -1;
    }
    task->socket_fd = fd;
    pthread_mutex_unlock(&manager->lock);
    return 0;
}

static void task_close_socket(transfer_task_t *task)
{
    transfer_manager_t *manager = task->manager;
    int fd;

    pthread_mutex_lock(&manager->lock);
    fd = task->socket_fd;
    task->socket_fd = -1;
    pthread_mutex_unlock(&manager->lock);
    if (fd >= 0) {
        client_connection_close(fd);
    }
}

static int run_upload(transfer_task_t *task)
{
    upload_request_payload_t request;
    resume_payload_t resume;
    packet_t packet = {0};
    struct stat st;
    uint64_t file_size;
    uint64_t offset;
    uint64_t remaining;
    unsigned char buffer[FILE_CHUNK_SIZE];
    int file_fd = -1;
    int result = -1;

    file_fd = open(task->local_path, O_RDONLY);
    if (file_fd < 0 || fstat(file_fd, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size < 0) {
        LOG_ERROR("cannot open upload source %s: %s", task->local_path,
                  strerror(errno));
        goto done;
    }
    file_size = (uint64_t)st.st_size;

    memset(&request, 0, sizeof(request));
    if (copy_string(request.remote_path, sizeof(request.remote_path),
                    task->remote_path) != 0 ||
        task_compute_sha256(task, file_fd, request.sha256_hex) != 0 ||
        lseek(file_fd, 0, SEEK_SET) == (off_t)-1) {
        LOG_ERROR("cannot prepare upload source %s", task->local_path);
        goto done;
    }
    request.file_size = host_to_net_u64(file_size);

    if (task_send(task, CMD_PUTS_REQ, STATUS_OK,
                  &request, sizeof(request)) != 0) {
        goto done;
    }
    if (task_recv(task, &packet) != 0) {
        goto done;
    }
    if (packet.header.type != CMD_PUTS_RESP ||
        packet.header.status != STATUS_OK ||
        packet.header.payload_len != sizeof(resume)) {
        LOG_ERROR("invalid upload resume response for %s", task->remote_path);
        packet_release(&packet);
        goto done;
    }
    memcpy(&resume, packet.payload, sizeof(resume));
    packet_release(&packet);
    offset = net_to_host_u64(resume.offset);
    if (offset > file_size) {
        task_report_error(task, STATUS_PROTOCOL_ERROR);
        goto done;
    }

    /* Even instant uploads end with a final ACK. */
    if (offset == file_size) {
        if (task_recv(task, &packet) == 0 &&
            packet.header.type == CMD_ACK &&
            packet.header.status == STATUS_OK) {
            result = 0;
        }
        packet_release(&packet);
        goto done;
    }
    if (lseek(file_fd, (off_t)offset, SEEK_SET) == (off_t)-1) {
        task_report_error(task, STATUS_IO_ERROR);
        goto done;
    }

    remaining = file_size - offset;
    while (remaining > 0) {
        size_t wanted = remaining > sizeof(buffer) ? sizeof(buffer) :
                        (size_t)remaining;
        ssize_t nread;

        if (task_is_canceled(task)) {
            goto done;
        }
        nread = read(file_fd, buffer, wanted);
        if (nread < 0 && errno == EINTR) {
            continue;
        }
        if (nread <= 0) {
            LOG_ERROR("upload source changed while reading %s", task->local_path);
            task_report_error(task, STATUS_IO_ERROR);
            goto done;
        }
        if (task_send(task, CMD_FILE_DATA, STATUS_OK,
                      buffer, (uint32_t)nread) != 0) {
            goto done;
        }
        remaining -= (uint64_t)nread;
    }

    if (task_send(task, CMD_FILE_END, STATUS_OK, NULL, 0) != 0) {
        goto done;
    }
    if (task_recv(task, &packet) != 0) {
        goto done;
    }
    if (packet.header.type == CMD_ACK && packet.header.status == STATUS_OK) {
        result = 0;
    } else {
        LOG_ERROR("upload rejected for %s", task->remote_path);
    }
    packet_release(&packet);

done:
    if (file_fd >= 0) {
        close(file_fd);
    }
    return result;
}

static int hash_matches(transfer_task_t *task, int fd, uint64_t size,
                        const char *expected)
{
    struct stat st;
    char actual[65];

    if (fstat(fd, &st) != 0 || st.st_size < 0 ||
        (uint64_t)st.st_size != size ||
        task_compute_sha256(task, fd, actual) != 0) {
        return 0;
    }
    return strcasecmp(actual, expected) == 0;
}

static int run_download(transfer_task_t *task)
{
    file_info_payload_t info;
    resume_payload_t resume;
    packet_t packet = {0};
    struct stat st;
    uint64_t file_size;
    uint64_t offset = 0;
    uint64_t received = 0;
    int file_fd = -1;
    int use_final = 0;
    int result = -1;

    if (task_send(task, CMD_GETS_REQ, STATUS_OK, task->remote_path,
                  (uint32_t)strlen(task->remote_path)) != 0) {
        goto done;
    }
    if (task_recv(task, &packet) != 0) {
        goto done;
    }
    if (packet.header.type != CMD_GETS_RESP ||
        packet.header.status != STATUS_OK ||
        packet.header.payload_len != sizeof(info)) {
        LOG_ERROR("invalid download metadata for %s", task->remote_path);
        packet_release(&packet);
        goto done;
    }
    memcpy(&info, packet.payload, sizeof(info));
    packet_release(&packet);
    info.file_name[sizeof(info.file_name) - 1] = '\0';
    info.sha256_hex[sizeof(info.sha256_hex) - 1] = '\0';
    file_size = net_to_host_u64(info.file_size);
    if (!utils_is_valid_sha256_hex(info.sha256_hex)) {
        task_report_error(task, STATUS_PROTOCOL_ERROR);
        goto done;
    }
    file_fd = open(task->target_path, O_RDONLY);
    if (file_fd >= 0 &&
        hash_matches(task, file_fd, file_size, info.sha256_hex)) {
        use_final = 1;
        offset = file_size;
    } else {
        if (file_fd >= 0) {
            close(file_fd);
            file_fd = -1;
        }
        file_fd = open(task->part_path, O_CREAT | O_RDWR, 0666);
        if (file_fd < 0 || fstat(file_fd, &st) != 0 || st.st_size < 0) {
            task_report_error(task, STATUS_IO_ERROR);
            goto done;
        }
        offset = (uint64_t)st.st_size;
        if (offset > file_size) {
            if (ftruncate(file_fd, 0) != 0) {
                task_report_error(task, STATUS_IO_ERROR);
                goto done;
            }
            offset = 0;
        }
        if (lseek(file_fd, (off_t)offset, SEEK_SET) == (off_t)-1) {
            task_report_error(task, STATUS_IO_ERROR);
            goto done;
        }
    }

    resume.offset = host_to_net_u64(offset);
    if (task_send(task, CMD_RESUME_POS, STATUS_OK,
                  &resume, sizeof(resume)) != 0) {
        goto done;
    }

    while (received < file_size - offset) {
        uint64_t remaining = file_size - offset - received;

        if (task_is_canceled(task)) {
            goto done;
        }
        if (task_recv(task, &packet) != 0) {
            goto done;
        }
        if (packet.header.type != CMD_FILE_DATA ||
            packet.header.status != STATUS_OK ||
            packet.header.payload_len == 0 ||
            packet.header.payload_len > FILE_CHUNK_SIZE ||
            packet.header.payload_len > remaining) {
            packet_release(&packet);
            task_report_error(task, STATUS_PROTOCOL_ERROR);
            goto done;
        }
        if (write_n(file_fd, packet.payload, packet.header.payload_len) != 0) {
            packet_release(&packet);
            task_report_error(task, STATUS_IO_ERROR);
            goto done;
        }
        received += packet.header.payload_len;
        packet_release(&packet);
    }

    if (task_recv(task, &packet) != 0) {
        goto done;
    }
    if (packet.header.type != CMD_FILE_END ||
        packet.header.status != STATUS_OK || packet.header.payload_len != 0) {
        packet_release(&packet);
        task_report_error(task, STATUS_PROTOCOL_ERROR);
        goto done;
    }
    packet_release(&packet);
    if (!use_final) {
        if (fsync(file_fd) != 0 ||
            !hash_matches(task, file_fd, file_size, info.sha256_hex)) {
            task_report_error(task, STATUS_PROTOCOL_ERROR);
            goto done;
        }
        close(file_fd);
        file_fd = -1;
        if (rename(task->part_path, task->target_path) != 0) {
            task_report_error(task, STATUS_IO_ERROR);
            goto done;
        }
    }
    if (task_send(task, CMD_ACK, STATUS_OK, NULL, 0) != 0) {
        goto done;
    }
    result = 0;

done:
    if (file_fd >= 0) {
        close(file_fd);
    }
    return result;
}

static void finish_task(transfer_task_t *task, int operation_result)
{
    transfer_manager_t *manager = task->manager;
    transfer_task_t **link;

    task->event->result = task_is_canceled(task) ? TRANSFER_RESULT_CANCELED :
                          (operation_result == 0 ? TRANSFER_RESULT_OK :
                           TRANSFER_RESULT_FAILED);

    pthread_mutex_lock(&manager->lock);
    link = &manager->active;
    while (*link != NULL && *link != task) {
        link = &(*link)->next;
    }
    if (*link == task) {
        *link = task->next;
        manager->active_count--;
    }
    task->event->next = NULL;
    if (manager->events_tail == NULL) {
        manager->events_head = task->event;
    } else {
        manager->events_tail->next = task->event;
    }
    manager->events_tail = task->event;
    if (manager->active_count == 0) {
        pthread_cond_broadcast(&manager->idle);
    }
    pthread_mutex_unlock(&manager->lock);
    free(task);
}

static void *transfer_worker(void *argument)
{
    transfer_task_t *task = argument;
    int fd;
    int result = -1;

    fd = client_connection_open(task->host, task->port,
                                task->connect_timeout_ms,
                                task->io_timeout_ms);
    if (fd < 0) {
        LOG_ERROR("cannot open transfer connection to %s:%s", task->host,
                  task->port);
    } else if (task_publish_socket(task, fd) != 0) {
        client_connection_close(fd);
    } else {
        result = task->kind == TRANSFER_UPLOAD ? run_upload(task) :
                                                run_download(task);
        task_close_socket(task);
    }
    finish_task(task, result);
    return NULL;
}

transfer_manager_t *transfer_manager_create(size_t max_concurrent)
{
    transfer_manager_t *manager;

    if (max_concurrent == 0) {
        return NULL;
    }
    manager = calloc(1, sizeof(*manager));
    if (manager == NULL) {
        return NULL;
    }
    if (pthread_mutex_init(&manager->lock, NULL) != 0) {
        free(manager);
        return NULL;
    }
    if (pthread_cond_init(&manager->idle, NULL) != 0) {
        pthread_mutex_destroy(&manager->lock);
        free(manager);
        return NULL;
    }
    manager->max_concurrent = max_concurrent;
    return manager;
}

void transfer_manager_stop(transfer_manager_t *manager)
{
    transfer_task_t *task;

    if (manager == NULL) {
        return;
    }
    pthread_mutex_lock(&manager->lock);
    manager->stopping = 1;
    for (task = manager->active; task != NULL; task = task->next) {
        atomic_store_explicit(&task->canceled, 1, memory_order_release);
        if (task->socket_fd >= 0) {
            (void)shutdown(task->socket_fd, SHUT_RDWR);
        }
    }
    while (manager->active_count != 0) {
        pthread_cond_wait(&manager->idle, &manager->lock);
    }
    pthread_mutex_unlock(&manager->lock);
}

void transfer_manager_drain_events(transfer_manager_t *manager)
{
    transfer_event_t *events;

    if (manager == NULL) {
        return;
    }
    pthread_mutex_lock(&manager->lock);
    events = manager->events_head;
    manager->events_head = NULL;
    manager->events_tail = NULL;
    pthread_mutex_unlock(&manager->lock);

    while (events != NULL) {
        transfer_event_t *next = events->next;
        const char *operation = events->kind == TRANSFER_UPLOAD ?
                                "upload" : "download";

        if (events->result == TRANSFER_RESULT_OK) {
            fprintf(stdout, "%s completed: %s\n", operation, events->path);
        } else if (events->result == TRANSFER_RESULT_CANCELED) {
            fprintf(stderr, "%s canceled: %s\n", operation, events->path);
        } else {
            fprintf(stderr, "%s failed: %s\n", operation, events->path);
        }
        free(events);
        events = next;
    }
}

void transfer_manager_destroy(transfer_manager_t *manager)
{
    transfer_event_t *event;

    if (manager == NULL) {
        return;
    }
    event = manager->events_head;
    while (event != NULL) {
        transfer_event_t *next = event->next;
        free(event);
        event = next;
    }
    pthread_cond_destroy(&manager->idle);
    pthread_mutex_destroy(&manager->lock);
    free(manager);
}

static int snapshot_task(transfer_task_t *task, client_runtime_t *runtime)
{
    char username[sizeof(runtime->username)];
    char cwd[PATH_MAX];

    if (client_runtime_session_snapshot(runtime, &task->session_id,
                                        username, sizeof(username),
                                        cwd, sizeof(cwd)) != 0 ||
        copy_string(task->host, sizeof(task->host),
                    runtime->config.remote.host) != 0 ||
        copy_string(task->port, sizeof(task->port),
                    runtime->config.remote.port) != 0) {
        return -1;
    }
    task->connect_timeout_ms = runtime->config.transfer.connect_timeout_ms;
    task->io_timeout_ms = runtime->config.transfer.io_timeout_ms;
    return copy_string(task->remote_path, sizeof(task->remote_path), cwd);
}

static int start_task(transfer_manager_t *manager, transfer_task_t *task)
{
    pthread_attr_t attributes;
    pthread_t thread;
    transfer_task_t *active;
    int created;

    pthread_mutex_lock(&manager->lock);
    if (manager->stopping || manager->active_count >= manager->max_concurrent) {
        pthread_mutex_unlock(&manager->lock);
        return -1;
    }
    if (task->kind == TRANSFER_DOWNLOAD) {
        for (active = manager->active; active != NULL; active = active->next) {
            if (active->kind == TRANSFER_DOWNLOAD &&
                strcmp(active->target_path, task->target_path) == 0) {
                pthread_mutex_unlock(&manager->lock);
                return -1;
            }
        }
    }
    task->next = manager->active;
    manager->active = task;
    manager->active_count++;

    if (pthread_attr_init(&attributes) != 0) {
        manager->active = task->next;
        manager->active_count--;
        pthread_mutex_unlock(&manager->lock);
        return -1;
    }
    if (pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED) != 0) {
        pthread_attr_destroy(&attributes);
        manager->active = task->next;
        manager->active_count--;
        pthread_mutex_unlock(&manager->lock);
        return -1;
    }
    created = pthread_create(&thread, &attributes, transfer_worker, task);
    pthread_attr_destroy(&attributes);
    if (created != 0) {
        manager->active = task->next;
        manager->active_count--;
        pthread_mutex_unlock(&manager->lock);
        return -1;
    }
    pthread_mutex_unlock(&manager->lock);
    return 0;
}

static transfer_task_t *allocate_task(transfer_manager_t *manager,
                                      transfer_kind_t kind)
{
    transfer_task_t *task = calloc(1, sizeof(*task));

    if (task == NULL) {
        return NULL;
    }
    task->event = calloc(1, sizeof(*task->event));
    if (task->event == NULL) {
        free(task);
        return NULL;
    }
    task->manager = manager;
    task->kind = kind;
    task->socket_fd = -1;
    atomic_init(&task->canceled, 0);
    task->event->kind = kind;
    return task;
}

static void free_unstarted_task(transfer_task_t *task)
{
    if (task != NULL) {
        free(task->event);
        free(task);
    }
}

int transfer_submit_upload(transfer_manager_t *manager,
                           client_runtime_t *runtime,
                           const char *local_path)
{
    transfer_task_t *task;
    char cwd[PATH_MAX];
    char base_name[NAME_MAX];

    if (manager == NULL || runtime == NULL || local_path == NULL ||
        local_path[0] == '\0') {
        return -1;
    }
    task = allocate_task(manager, TRANSFER_UPLOAD);
    if (task == NULL || snapshot_task(task, runtime) != 0 ||
        copy_string(cwd, sizeof(cwd), task->remote_path) != 0 ||
        utils_expand_local_path(local_path, task->local_path,
                                sizeof(task->local_path)) != 0 ||
        utils_get_base_name(task->local_path, base_name,
                            (int)sizeof(base_name)) != 0) {
        free_unstarted_task(task);
        return -1;
    }
    if (normalize_cd_path(cwd, base_name, task->remote_path,
                          sizeof(task->remote_path)) != 0 ||
        copy_string(task->event->path, sizeof(task->event->path),
                    task->remote_path) != 0 ||
        start_task(manager, task) != 0) {
        free_unstarted_task(task);
        return -1;
    }
    return 0;
}

int transfer_submit_download(transfer_manager_t *manager,
                             client_runtime_t *runtime,
                             const char *remote_path)
{
    transfer_task_t *task;
    char cwd[PATH_MAX];
    char download_dir[PATH_MAX];
    char base_name[NAME_MAX];
    size_t target_length;

    if (manager == NULL || runtime == NULL || remote_path == NULL ||
        remote_path[0] == '\0') {
        return -1;
    }
    task = allocate_task(manager, TRANSFER_DOWNLOAD);
    if (task == NULL || snapshot_task(task, runtime) != 0 ||
        copy_string(cwd, sizeof(cwd), task->remote_path) != 0 ||
        copy_string(download_dir, sizeof(download_dir),
                    runtime->download_dir) != 0 ||
        normalize_cd_path(cwd, remote_path, task->remote_path,
                          sizeof(task->remote_path)) != 0 ||
        utils_get_base_name(task->remote_path, base_name,
                            (int)sizeof(base_name)) != 0 ||
        join_path(task->target_path, sizeof(task->target_path),
                  download_dir, base_name) != 0) {
        free_unstarted_task(task);
        return -1;
    }
    target_length = strlen(task->target_path);
    if (target_length + sizeof(".part") > sizeof(task->part_path)) {
        free_unstarted_task(task);
        return -1;
    }
    memcpy(task->part_path, task->target_path, target_length);
    memcpy(task->part_path + target_length, ".part", sizeof(".part"));
    if (copy_string(task->event->path, sizeof(task->event->path),
                    task->target_path) != 0) {
        free_unstarted_task(task);
        return -1;
    }

    if (start_task(manager, task) != 0) {
        free_unstarted_task(task);
        return -1;
    }
    return 0;
}
