# Implementation Details

This document records cross-phase implementation decisions. It explains why the server and protocol are structured this way, while phase-specific command flows are documented separately in `phase-1-basic.md` and `phase-2-resume.md`.

## Thread Pool Design

### Interface and Implementation Boundary

The `thread_pool` module is designed as an Abstract Data Type. Code outside the module should know what a thread pool can do, but should not know how the thread pool stores its internal state.

The public interface is declared in `include/server/thread_pool.h`:

```c
typedef struct thread_pool thread_pool_t;

thread_pool_t *thread_pool_create(int thread_num, int queue_capacity);
int thread_pool_add(thread_pool_t *pool, void (*function)(void *), void *arg);
void thread_pool_destroy(thread_pool_t *pool);
```

The first line is a forward declaration. It tells callers that `struct thread_pool` exists and gives it the alias `thread_pool_t`, but it does not expose the fields inside the struct. Callers can only hold a `thread_pool_t *` pointer and pass it back to the functions provided by the module.

The real definition of `struct thread_pool` is kept in `src/server/thread_pool.c`:

```c
struct thread_pool {
    pthread_t* threads;
    int thread_num;
    task_queue_t queue;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    int shutdown;
};
```

This separation gives the module a clear boundary:

- Interface: create a pool, add a task, destroy the pool.
- Implementation: worker threads, task queue layout, locking, condition variables, shutdown behavior, and memory ownership.

The same idea also reduces compile-time coupling. `thread_pool.h` does not need to include `<pthread.h>` because the header does not expose `pthread_t` or `pthread_mutex_t`. Only `thread_pool.c` includes `<pthread.h>`, since only the implementation needs to know those concrete types.

### Task Abstraction

The task stored in the queue is also abstract. In `src/server/thread_pool.c`, a task is defined as:

```c
typedef struct task {
    void (*function)(void*);
    void* arg;
    struct task* next;
} task_t;
```

The thread pool does not know the concrete type of the work item. It only knows that every task has two parts:

- `function`: the code that should be executed by a worker thread.
- `arg`: the argument passed to that function.

This keeps the pool independent from business concepts such as client fds, packets, login requests, or file-transfer requests. The producer decides what the task means, while the thread pool decides when and where it runs.

### Task Queue Instead of FD Queue

The server queues executable tasks, not raw client fds.

If the queue stored only fds, a worker thread would still need to read from the socket, parse the packet, identify the command, and then execute the real business logic. In that design, connection management and task execution are mixed together. A slow or incomplete socket read could also occupy a worker before there is a complete request to process.

In the current implementation, the epoll loop owns connection readiness and packet receiving. After a complete request is ready, the server wraps the work into a function plus argument and submits that task to the pool.

This has several benefits:

- Worker threads spend time on real work instead of waiting for socket readiness.
- The thread pool does not depend on socket details or protocol details.
- Different task types can share the same pool as long as they fit `function + arg`.
- The main event loop stays focused on connection events.

## Server Event Loop and Task Dispatch

### Epoll Responsibilities

The server main thread uses epoll to monitor:

- The listening socket, used to accept new clients.
- Client sockets, used to receive the first packet of a request.
- The self-pipe read end, used to wake the event loop during Ctrl+C shutdown.

The main thread does not execute command logic directly. Its job is to accept connections, detect disconnects, read complete request packets, and dispatch work to the thread pool.

### Normal Packet Tasks

Normal commands are handled through `packet_task_t`:

```c
typedef struct {
    int client_fd;
    packet_t packet;
    const auth_config_t *auth_config;
    const char *cloud_drive_root;
} packet_task_t;
```

Commands such as `pwd`, `cd`, `ls`, `mkdir`, `rmdir`, and `rm` have a request/response shape. The server can receive one request packet, submit it to a worker, let the worker send one response, and then return to normal epoll monitoring.

These commands are dispatched to `handle_packet_task`.

### File Transfer Tasks

File-transfer commands are handled through `transfer_task_t`:

```c
typedef struct {
    int epoll_fd;
    int client_fd;
    packet_t packet;
    const auth_config_t *auth_config;
    const char *cloud_drive_root;
} transfer_task_t;
```

`puts` and `gets` are not single-packet commands. They start with one request packet and then continue with a stream of file-data packets, resume offsets, end markers, and final acknowledgements.

Because of that, they are dispatched to `handle_transfer_task` instead of `handle_packet_task`.

### FD Ownership During Transfer

During a file transfer, the worker temporarily owns the client fd.

The dispatch flow is:

1. The epoll loop receives `CMD_PUTS_REQ` or `CMD_GETS_REQ`.
2. The server creates a `transfer_task_t` from the first request packet.
3. The client fd is removed from epoll with `EPOLL_CTL_DEL`.
4. A worker runs the full transfer protocol on that fd.
5. After the transfer returns, the fd is added back to epoll with `EPOLLIN | EPOLLRDHUP`.

This design prevents epoll from reporting more events for the same fd while a worker is already reading or writing the file-transfer stream. It also prevents different workers from accidentally splitting one transfer into unordered packet tasks.

## File Transfer Design

### Why Transfer Is Not Split Per Packet

File data is transferred as many `CMD_FILE_DATA` packets. If every packet were submitted as an independent thread-pool task, packet processing order would no longer be guaranteed. A later file chunk might be handled before an earlier one.

To make that design correct, the protocol would need sequence numbers, missing-packet detection, buffering, reordering, and more complex end-of-transfer rules. That is unnecessary for the current TCP-based design.

Instead, one worker owns the transfer and processes the stream in order.

### Ordered Stream Transfer

The current transfer model relies on TCP ordering and one worker reading or writing the socket continuously. The sender can stream file chunks without waiting for an ACK after every chunk, and the receiver can write chunks in the same order they arrive.

This avoids the low throughput of stop-and-wait transfers while keeping the protocol much simpler than a sequence-number design.

### Resume Offset Ownership

Upload and download resume offsets are owned by different sides:

- For `puts`, the server owns the resume offset because it owns the partial remote file. The server returns the existing target file size in `CMD_PUTS_RESP`.
- For `gets`, the client owns the resume offset because it owns the partial local file. The client sends the existing local download size in `CMD_RESUME_POS`.

This keeps each side responsible for the file state it can verify locally.

### Large File Path

Phase 2 optimizes large transfers without changing the external command interface:

- Client upload uses normal `read` for small remaining data.
- Client upload uses `mmap` when the remaining data is larger than `FILE_OPTIMIZATION_THRESHOLD`.
- Server download sends each TLV data header with `send_packet_header`, then sends the matching file payload with `sendfile`.

The protocol still sees ordinary `CMD_FILE_DATA` packets. The optimization changes only how the local process reads or sends file bytes.

## Protocol Helpers

### TLV Packet Boundary

Each packet starts with a fixed TLV header:

```c
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t cmd_type;
    uint32_t status;
    uint32_t data_len;
} tlv_header_t;
```

`recv_packet` reads this header first, validates `magic`, `version`, and `data_len`, then allocates and reads the payload when `data_len > 0`.

This gives every command a clear packet boundary on top of TCP's byte stream.

### `send_packet` vs `send_packet_header`

Most responses use `send_packet`, which writes the TLV header and payload from a user-space buffer.

The server download path needs a separate helper because `sendfile` writes file bytes directly from a file descriptor to the socket. For that path, the server writes only the TLV header first:

```c
send_packet_header(client_fd, CMD_FILE_DATA, STATUS_OK, chunk_size);
sendfile_exact(client_fd, file_fd, &offset, chunk_size);
```

This preserves the same packet format while allowing the payload to come from `sendfile` instead of a user-space buffer.

### 64-bit Integer Byte Order

The protocol sends file sizes and resume offsets as `uint64_t`. Standard `htonl` and `ntohl` only handle 32-bit values, so the project provides:

```c
uint64_t host_to_net_u64(uint64_t value);
uint64_t net_to_host_u64(uint64_t value);
```

These helpers are used for `file_info_payload_t.file_size` and `resume_payload_t.offset`.

## Related Documents

- `docs/overview.md`: phase-level feature requirements.
- `docs/phase-1-basic.md`: Phase 1 source layout, command behavior, and basic protocol flows.
- `docs/phase-2-resume.md`: Phase 2 resumable transfer and large-file optimization details.
- `tests/resume-test.md`: manual resumable upload verification.
- `tests/mmap-test.md`: manual upload path verification for `read` and `mmap`.
