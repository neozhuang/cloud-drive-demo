# Cloud Drive Demo

A cloud drive demo implemented in C. The project is currently in Phase 2, with a TCP client/server framework, username login, per-user virtual directory isolation, basic file commands, resumable upload/download, large-file transfer optimization, and configurable logging.

## Current Technology

- Language and build: C, GCC, Makefile, pthread.
- Network model: TCP sockets; the server uses epoll to monitor connection and client events.
- Concurrency model: the main thread handles accept/epoll, and a worker thread pool handles command packets and file transfer tasks.
- Communication protocol: a custom TLV binary protocol with magic, version, command, status, and payload length fields.
- File transfer: fixed-size `CMD_FILE_DATA` packets, resumable transfer offsets, client-side `mmap` for large uploads, and server-side `sendfile` for downloads.
- Configuration: `config/server.conf`, `config/client.conf`, and `config/users.conf`.
- Logging: debug/info/warn/error log levels; server logs are written to `logs/server.log` by default.
- Storage model: each user is isolated under `data/users/<username>`, while the client sees a virtual `/` root.

## Implemented Features

- The client connects to the server and prompts for a username before entering the command loop.
- Supported commands: `pwd`, `cd`, `ls`, `mkdir`, `rmdir`, `rm`, `puts`, and `gets`.
- `puts <local-file>` uploads a local file to the current remote directory and resumes from the server-side partial file size when interrupted.
- `gets <remote-file>` downloads a remote file into local `downloads/` and resumes from the local partial file size when interrupted.
- Large uploads use `mmap` when the remaining file size is larger than `FILE_OPTIMIZATION_THRESHOLD`.
- Downloads are sent by the server with `sendfile` after the TLV packet header is written.
- File transfer commands are handled as dedicated transfer tasks: the client fd is temporarily removed from epoll during transfer and added back after the transfer completes.
- The server automatically creates the cloud drive root and per-user directories.
- The server supports graceful Ctrl+C shutdown and cleans up the thread pool, epoll fd, log module, and related resources.

## Build and Run

```bash
make
./bin/server-cdd
./bin/client-cdd
```

By default, the server listens on `0.0.0.0:8888`, and the client connects to `127.0.0.1:8888`. These values can be changed through the configuration files.

## Demo Video

[Phase 1 Basic Usage Demo](https://github.com/user-attachments/assets/85da4ad5-ecd6-43ad-b71a-10fdc79ffe54)

## Detailed Documentation

For Phase 1 technical details, source structure, module responsibilities, and basic protocol flows, see [docs/phase-1-basic.md](docs/phase-1-basic.md).

For Phase 2 resumable transfer and large-file optimization details, see [docs/phase-2-resume.md](docs/phase-2-resume.md).

For cross-phase implementation details such as the thread pool, epoll dispatch, transfer task ownership, and protocol helpers, see [docs/details.md](docs/details.md).

Related verification notes are available in [tests/resume-test.md](tests/resume-test.md) and [tests/mmap-test.md](tests/mmap-test.md).
