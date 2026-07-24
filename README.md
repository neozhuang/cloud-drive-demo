# Cloud Drive Demo

Cloud Drive Demo is a command-line cloud drive implemented in C. The current Phase 4 implementation combines MySQL-backed users and virtual paths, content-addressed file storage, resumable transfers, and SessionID-based concurrent control and transfer connections.

## Current Technology

- Language and build: C, GCC, Make, and pthreads.
- Network model: TCP sockets with a server-side epoll event loop.
- Concurrency model: the main server thread handles accept and epoll events, while a worker pool handles commands and transfers. Client transfers run in background threads.
- Protocol: a custom TLV2 binary protocol with a 36-byte header, including a server-issued 16-byte SessionID.
- Session model: an in-memory SessionID index and fd index allow one authenticated session to own a control connection and multiple transfer connections.
- Database: MySQL/MariaDB stores users, virtual paths, physical file metadata, hashes, sizes, and reference counts.
- Database client: `libmysqlclient` with a server-side connection pool.
- Hashing: OpenSSL EVP SHA-256 for content addressing, upload verification, instant upload, and download validation.
- File transfer: resumable `CMD_FILE_DATA` payloads of up to 4096 bytes and server-side `sendfile` for download data.
- Idle management: a `CLOCK_MONOTONIC` timerfd advances a timing wheel for idle control connections.
- Configuration: `config/server.conf` and `config/client.conf`.
- Logging: debug, info, warn, and error levels with separate client and server log files.
- Storage: logical paths are stored in MySQL, file content is stored under `data/files/<sha256_hex>`, and interrupted uploads use `data/.upload/<sha256_hex>.part`.

## Implemented Features

- Registration and login with password hashes stored in MySQL.
- Shell-like commands: `pwd`, `cd`, `ls`, `mkdir`, `rmdir`, `rm`, `puts`, and `gets`.
- Per-user virtual directory trees backed by the `paths` table.
- SHA-256 content-addressed storage and reference-counted physical files.
- Instant upload when identical content already exists on the server.
- Resumable upload from a server-side partial file and resumable download from a client-side partial file.
- Download validation before the `.part` file is renamed to its final name.
- A persistent control connection for authentication and short commands.
- One independent TCP connection and background thread for each `puts` or `gets` task.
- Configurable client-side transfer concurrency and socket timeouts.
- A 128-bit random SessionID shared by all connections belonging to one login session.
- Shared virtual cwd state across a session, with transfer paths snapshotted when a task is submitted.
- Idle control-connection cleanup through a timerfd-driven timing wheel without interrupting authenticated transfers.
- Control-socket recovery after disconnect: the client reconnects and returns to the login menu without automatically re-authenticating or replaying the failed command.

Transfer connections are single-purpose. The server removes a transfer fd from epoll and the idle timing wheel after its first authenticated request, lets one worker own it for the transfer, and then detaches and closes it. Only a control fd is returned to epoll after a short command completes.

## Build And Run

The build requires GCC, pthreads, OpenSSL, `libcrypt`, and the MySQL/MariaDB client development library.

```bash
make
./bin/server-cdd
./bin/client-cdd
```

Before starting the server, create the database and configure the `[mysql]` section in `config/server.conf`:

```sql
CREATE DATABASE cloud_drive_demo DEFAULT CHARACTER SET utf8mb4;
```

The server creates missing tables and configured storage directories during startup. The default storage and idle-control settings are:

```ini
[storage]
root_dir = data/files
transfer_temp_dir = data/.upload

[session]
idle_timeout_seconds = 30
```

Client transfer limits and socket timeouts are configured in `config/client.conf`:

```ini
[transfer]
max_concurrent = 4
connect_timeout_ms = 5000
io_timeout_ms = 30000
```

By default, the server listens on `127.0.0.1:8888`, and the client connects to the same address. Both values can be changed in the configuration files.

## Tests

Build and run the Phase 4 unit tests with:

```bash
make protocol-test session-test timer-wheel-test \
  client-runtime-test client-connection-test client-command-test
```

These targets cover TLV2 SessionID framing, server session binding and lifecycle, idle timing-wheel behavior, client session snapshots, control-connection checks, and reconnect-result handling. The repository does not currently provide a single aggregate `make test` target.

## Demo Videos

[Phase 1 Usage Demo](https://github.com/user-attachments/assets/85da4ad5-ecd6-43ad-b71a-10fdc79ffe54)

[Phase 3 Usage Demo](https://github.com/user-attachments/assets/20e2f894-3e03-406c-8735-2c968cfd451e)

## Documentation

- [Project overview and phase evolution](docs/overview.md)
- [Phase 1 basic framework](docs/phase-1-basic.md)
- [Phase 2 resumable transfers](docs/phase-2-resume.md)
- [Phase 3 database-backed metadata](docs/phase-3-database.md)
- [Phase 4 SessionID and connection lifecycle](docs/phase-4-session-id.md)
- [Cross-phase implementation details](docs/details.md)

The Phase 1-3 documents are historical phase records. Earlier connection-lifecycle and upload-optimization descriptions may differ from the current Phase 4 implementation.

## Current Limitations

- SessionIDs are process-local bearer credentials and are not persisted across restarts.
- There is no explicit logout, server-side revocation, or fixed SessionID expiry policy.
- Transport is unencrypted TCP; TLS is not implemented.
- The client reconnects a lost control socket but does not persist credentials, automatically log in again, or replay the failed command.
