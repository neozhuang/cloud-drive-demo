# Implementation Details

This document records implementation decisions across project phases. It is organized by phase because the project architecture evolves over time: early phases favor simpler filesystem-backed behavior, while later phases add resumable transfer, database-backed metadata, content-addressed storage, and more explicit session lifecycle handling.

Phase-specific user-facing flows are documented in separate files. This document focuses on cross-cutting implementation choices and why the code is structured this way.

## Phase 1: Basic Client/Server Framework

Phase 1 establishes the TCP server, interactive client, TLV protocol, thread pool, epoll event loop, and basic command dispatch model.

### Thread Pool Design

#### Interface and Implementation Boundary

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

#### Task Abstraction

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

#### Task Queue Instead of FD Queue

The server queues executable tasks, not raw client fds.

If the queue stored only fds, a worker thread would still need to read from the socket, parse the packet, identify the command, and then execute the real business logic. In that design, connection management and task execution are mixed together. A slow or incomplete socket read could also occupy a worker before there is a complete request to process.

In the current implementation, the epoll loop owns connection readiness and packet receiving. After a complete request is ready, the server wraps the work into a function plus argument and submits that task to the pool.

This has several benefits:

- Worker threads spend time on real work instead of waiting for socket readiness.
- The thread pool does not depend on socket details or protocol details.
- Different task types can share the same pool as long as they fit `function + arg`.
- The main event loop stays focused on connection events.

### Server Event Loop

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
    database_pool_t* db_pool;
    char storage_root[PATH_MAX];
} packet_task_t;
```

Commands such as `pwd`, `cd`, `ls`, `mkdir`, `rmdir`, and `rm` have a request/response shape. The server can receive one request packet, submit it to a worker, let the worker send one response, and then return to normal epoll monitoring.

These commands are dispatched to `handle_basic_task`.

### Basic TLV Protocol

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

### Basic Session Model

At the framework level, a session connects one client socket fd to runtime user state. Early command handlers need to know whether a client has logged in, which user it represents, and what virtual current directory it is using.

In later phases this session state becomes database-aware, but the core responsibility remains the same: the server must map a connection to user context before executing commands.

## Phase 2: Resumable And Optimized Transfer

Phase 2 adds resumable upload/download and large-file transfer optimization. It keeps the TLV protocol but changes file-transfer commands so both sides can agree on resume offsets before file data is sent.

### File Transfer Tasks

File-transfer commands are handled through `transfer_task_t`:

```c
typedef struct {
    int epoll_fd;
    int client_fd;
    packet_t packet;
    database_pool_t* db_pool;
    char storage_root[PATH_MAX];
    char transfer_temp_dir[PATH_MAX];
} transfer_task_t;
```

`puts` and `gets` are not single-packet commands. They start with one request packet and then continue with a stream of file-data packets, resume offsets, end markers, and final acknowledgements.

Because of that, they are dispatched to `handle_transfer_task` instead of `handle_basic_task`.

### FD Ownership During Transfer

During a file transfer, the worker temporarily owns the client fd.

The dispatch flow is:

1. The epoll loop receives `CMD_PUTS_REQ` or `CMD_GETS_REQ`.
2. The server creates a `transfer_task_t` from the first request packet.
3. The client fd is removed from epoll with `EPOLL_CTL_DEL`.
4. A worker runs the full transfer protocol on that fd.
5. After the transfer returns, the fd is added back to epoll with `EPOLLIN | EPOLLRDHUP`.

This design prevents epoll from reporting more events for the same fd while a worker is already reading or writing the file-transfer stream. It also prevents different workers from accidentally splitting one transfer into unordered packet tasks.

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

### Large Upload Path: `mmap`

For large uploads, the client can map the local file into memory and send chunks directly from the mapped region.

The resume offset may not be page-aligned, but `mmap` requires an aligned offset. The client handles this by mapping from the previous page boundary:

```c
uint64_t map_offset = file_offset / page_size * page_size;
uint64_t offset_delta = file_offset - map_offset;
uint64_t map_len = file_size - map_offset;
char* map = mmap(NULL, map_len, PROT_READ, MAP_SHARED, file_fd, map_offset);
```

The actual first unsent byte is `map + offset_delta`.

### Download Path: `sendfile`

The server download path uses `sendfile` to send file data directly from the file descriptor to the client socket.

Because each file chunk still needs a TLV header, the server writes the packet header first:

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

## Phase 3: Database-Backed Metadata

Phase 3 replaces the filesystem-only metadata model with MySQL-backed users, paths, file records, hashes, and reference counts. The server still stores bytes on disk, but the database becomes authoritative for user-visible metadata.

### Database Support

The server initializes MySQL during startup and creates three core tables when they do not exist:

- `users`: application users and password hashes.
- `paths`: per-user virtual directory tree.
- `files`: physical file metadata, SHA-256 hash, size, and reference count.

A custom `database_pool_t` keeps reusable MySQL connections for worker threads.

### DAO Layer

The DAO layer keeps SQL away from command handlers.

Current DAO responsibilities include:

- `dao_auth`: registration and login metadata.
- `dao_basic`: virtual path lookup, directory commands, `rm`, and reference count handling.
- `dao_transfer`: file linking, file creation, and download metadata lookup.
- `dao_status`: shared status values such as `DAO_OK`, `DAO_NOT_FOUND`, `DAO_DB_ERROR`, and `DAO_SHOULD_DELETE_PHYSICAL`.

The handler layer should translate DAO results into protocol status codes such as `STATUS_FILE_NOTEXIST`, `STATUS_IS_DIR`, or `STATUS_DB_ERROR`.

### Instant Upload

If the server already has a `files` row with the same hash and size, the upload can be completed without sending file data.

The instant upload path:

1. The client sends `CMD_PUTS_REQ(file_name, size, sha256_hex)`.
2. The server finds an existing `files` row by hash and size.
3. The server increments `files.refs` and inserts a new `paths` row.
4. The server returns `CMD_PUTS_RESP(offset=file_size)`.
5. The client does not send file content.

This is possible because content identity is based on SHA-256 plus size, not on the user-visible file name.

### Reference Counting

The `files.refs` field counts how many logical paths reference one physical file.

When `rm` removes a logical file path, the DAO transaction removes the `paths` row and decrements `files.refs`.

If `refs` remains greater than zero, the physical file stays on disk. If `refs` reaches zero, the DAO removes the `files` row and returns `DAO_SHOULD_DELETE_PHYSICAL` with the hash hex. The handler can then unlink:

```text
storage_root/<hash_hex>
```

The database state is the user-visible source of truth. If `unlink` fails after a successful database transaction, the file becomes an orphan on disk and should be logged for later cleanup.

### Session Management

Phase 3 sessions are database-aware. A session does not only store a username; it also stores the database `user_id`, current virtual path id, and current virtual path string.

The current implementation uses an fd-indexed session table:

```c
static session_t **sessions;
static size_t sessions_cap;
static pthread_mutex_t sessions_table_lock;
```

Each accepted client fd is used directly as the array index:

```c
sessions[client_fd] = session;
```

This makes lookup, insertion, and deletion O(1). The tradeoff is that the array is sized by the process fd limit, so it may allocate more pointers than the number of active clients.

#### Session Design Comparison

The project considered several approaches:

| Design | Lookup | Insert | Delete | Concurrency | Complexity |
| --- | ---: | ---: | ---: | --- | --- |
| Linked list + global mutex | O(n) | O(1) | O(n) | Low | Lowest |
| fd array + global mutex | O(1) | O(1) | O(1) | Medium | Low |
| fd array + global rwlock | O(1) | O(1) | O(1) | Good for read-heavy workloads | Medium |
| hash table + global mutex | Avg O(1) | Avg O(1) | Avg O(1) | Medium | Medium |
| hash table + sharded locks | Avg O(1) | Avg O(1) | Avg O(1) | Good | Medium-high |
| fd array + per-session lock | O(1) | O(1) | O(1) | Good | High |
| hash table + per-session lock / RCU | Avg O(1) | Avg O(1) | Avg O(1) | Very good | Highest |

The current implementation is closest to the fd array plus per-session lock design. It is more concurrent than a single global linked list, but it requires careful lifecycle handling.

#### Session Object

Each session stores:

```c
struct session {
    pthread_mutex_t lock;
    int ref_count;
    int closing;
    int client_fd;
    int logged_in;
    uint64_t user_id;
    char username[64];
    uint64_t cwd_id;
    char cwd[PATH_MAX];
};
```

Important fields:

- `client_fd`: socket fd used as the session-table key.
- `logged_in`: whether authentication has succeeded.
- `user_id`: database user id from the `users` table.
- `username`: logged-in username.
- `cwd_id`: current virtual directory row id in the `paths` table.
- `cwd`: current virtual path string, such as `/` or `/docs`.
- `closing`: marks a session that is being destroyed.
- `lock`: protects mutable fields inside one session.

#### Table Lock vs Session Lock

The implementation uses two lock levels:

1. `sessions_table_lock` protects the global `sessions[]` table.
2. `session.lock` protects the fields inside one session.

Typical lookup flow:

```text
lock sessions_table_lock
    s = sessions[client_fd]
    lock s->lock
unlock sessions_table_lock

read or modify s fields

unlock s->lock
```

This keeps table lookup safe while allowing different sessions to be modified independently after they are found.

#### Session Lifecycle

When the server accepts a new client:

```text
accept()
    -> session_create(client_fd)
    -> network_add_epoll_fd(client_fd)
```

After `CMD_LOGIN_REQ` succeeds, the login handler calls:

```c
session_set_login_state(client_fd, user_id, username, root_path_id);
```

This marks the session as logged in and initializes the current virtual directory to `/`.

Directory changes update session state with:

```c
session_set_cwd(client_fd, cwd_id, virtual_path);
```

Command handlers read both user and cwd metadata with:

```c
session_get_location(client_fd, &user_id, &cwd_id, cwd, sizeof(cwd));
```

When epoll reports `EPOLLRDHUP`, `EPOLLHUP`, or `EPOLLERR`, the server closes the client:

```text
close_client()
    -> epoll_ctl(EPOLL_CTL_DEL)
    -> session_destroy(client_fd)
    -> close(client_fd)
```

`session_destroy` must remove the table entry:

```c
sessions[client_fd] = NULL;
```

This is important because operating systems commonly reuse low-numbered file descriptors. If the table entry is not cleared, a later connection may receive the same fd and `session_create` will incorrectly think a session already exists.


## Phase 4

