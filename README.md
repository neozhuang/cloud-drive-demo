# Cloud Drive Demo

A basic cloud drive demo implemented in C. The project is currently in Phase 1, with a TCP client/server framework, username login, per-user virtual directory isolation, basic file commands, file upload/download, and configurable logging.

## Current Technology

- Language and build: C, GCC, Makefile, pthread.
- Network model: TCP sockets; the server uses epoll to monitor connection and client events.
- Concurrency model: the main thread handles accept/epoll, and a worker thread pool handles command packets and file transfer tasks.
- Communication protocol: a custom TLV binary protocol with magic, version, command, status, and payload length fields.
- Configuration: `config/server.conf`, `config/client.conf`, and `config/users.conf`.
- Logging: debug/info/warn/error log levels; server logs are written to `logs/server.log` by default.
- Storage model: each user is isolated under `data/users/<username>`, while the client sees a virtual `/` root.

## Implemented Features

- The client connects to the server and prompts for a username before entering the command loop.
- Supported commands: `pwd`, `cd`, `ls`, `mkdir`, `rmdir`, `rm`, `puts`, and `gets`.
- `puts <local-file>` uploads a local file to the current remote directory.
- `gets <remote-file>` downloads a remote file into local `downloads/`.
- The server automatically creates the cloud drive root and per-user directories.
- The server supports graceful Ctrl+C shutdown and cleans up the thread pool, epoll fd, log module, and related resources.

## Build and Run

```bash
make
./bin/cdd-server
./bin/cdd-client
```

By default, the server listens on `0.0.0.0:8888`, and the client connects to `127.0.0.1:8888`. These values can be changed through the configuration files.

## Demo Video

[Phase 1 Basic Usage Demo](assets/videos/phase-1-basic-usage-demo.mp4)

## Detailed Documentation

For Phase 1 technical details, source structure, module responsibilities, and protocol flows, see [docs/phase-1-basic.md](docs/phase-1-basic.md).
