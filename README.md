# Cloud Drive Demo

A cloud drive demo implemented in C. The project is currently in Phase 3, with a TCP client/server framework, MySQL-backed users and metadata, per-user virtual directory isolation, content-addressed physical storage, instant upload, reference-counted deletion, resumable upload/download, large-file transfer optimization, and configurable logging.

## Current Technology

- Language and build: C, GCC, Makefile, pthread.
- Network model: TCP sockets; the server uses epoll to monitor connection and client events.
- Concurrency model: the main thread handles accept/epoll, and a worker thread pool handles command packets and file transfer tasks.
- Communication protocol: a custom TLV binary protocol with magic, version, command, status, and payload length fields.
- Database: MySQL/MariaDB stores users, virtual paths, physical file metadata, hashes, sizes, and reference counts.
- Database client: `libmysqlclient` with a custom server-side connection pool.
- Hashing: OpenSSL EVP SHA-256 for content addressing, upload verification, instant upload, and download validation.
- File transfer: fixed-size `CMD_FILE_DATA` packets, resumable transfer offsets, client-side `mmap` for large uploads, and server-side `sendfile` for downloads.
- Configuration: `config/server.conf` and `config/client.conf`.
- Logging: debug/info/warn/error log levels; server logs are written to `logs/server.log` by default.
- Storage model: user-visible paths are stored in MySQL; physical file bytes are stored under `data/files/<sha256_hex>`, and interrupted uploads are stored under `data/.upload/<sha256_hex>.part`.

## Implemented Features

- The client connects to the server and supports registration/login before entering the command loop.
- Supported commands: `pwd`, `cd`, `ls`, `mkdir`, `rmdir`, `rm`, `puts`, and `gets`.
- Users and password hashes are stored in MySQL.
- Each user's virtual directory tree is represented by the `paths` table.
- Physical file metadata is represented by the `files` table.
- Physical file content is addressed by SHA-256 hash and shared across users/paths when content is identical.
- `puts <local-file>` supports instant upload when the same content hash already exists in the database.
- `puts <local-file>` uploads a local file to the current remote directory and resumes from the server-side partial file size when interrupted.
- `gets <remote-file>` resolves the remote path through database metadata, downloads the physical object into local `downloads/`, and resumes from the local partial file size when interrupted.
- `rm <remote-file>` removes the logical path and deletes the physical file only when the file reference count reaches zero.
- Large uploads use `mmap` when the remaining file size is larger than `FILE_OPTIMIZATION_THRESHOLD`.
- Downloads are sent by the server with `sendfile` after the TLV packet header is written.
- File transfer commands are handled as dedicated transfer tasks: the client fd is temporarily removed from epoll during transfer and added back after the transfer completes.
- The server automatically creates missing database tables and configured storage directories during startup.
- The server supports graceful Ctrl+C shutdown and cleans up the thread pool, epoll fd, log module, and related resources.

## Build and Run

```bash
make
./bin/server-cdd
./bin/client-cdd
```

Before starting the server, create the MySQL database and configure the `[mysql]` section in `config/server.conf`:

```sql
CREATE DATABASE cloud_drive_demo DEFAULT CHARACTER SET utf8mb4;
```

The server creates missing tables during startup. Storage directories are configured by the `[storage]` section in `config/server.conf`:

```ini
root_dir = data/files
transfer_temp_dir = data/.upload
```

By default, the server listens on `127.0.0.1:8888`, and the client connects to `127.0.0.1:8888`. These values can be changed through the configuration files.

## Demo Video

[Phase 1 Usage Demo](https://github.com/user-attachments/assets/85da4ad5-ecd6-43ad-b71a-10fdc79ffe54)

[Phase 3 Usage Demo](https://github.com/user-attachments/assets/20e2f894-3e03-406c-8735-2c968cfd451e)

## Detailed Documentation

For Phase 1 technical details, source structure, module responsibilities, and basic protocol flows, see [docs/phase-1-basic.md](docs/phase-1-basic.md).

For Phase 2 resumable transfer and large-file optimization details, see [docs/phase-2-resume.md](docs/phase-2-resume.md).

For Phase 3 database-backed metadata, content-addressed storage, DAO layering, instant upload, and reference-counted deletion details, see [docs/phase-3-database.md](docs/phase-3-database.md).

For cross-phase implementation details such as the thread pool, epoll dispatch, transfer task ownership, protocol helpers, and session management, see [docs/details.md](docs/details.md).

For a high-level project overview, see [docs/overview.md](docs/overview.md).

Related verification notes are available in [tests/resume-test.md](tests/resume-test.md) and [tests/mmap-test.md](tests/mmap-test.md).
