# AGENTS.md

## Repository Shape
- This repo is split by learning phases: `phase1-basic-framework/`, `phase2-user-authentication/`, and `phase3-mysql/`; do not assume a single root build.
- `phase3-mysql/` is the active MySQL-based rewrite and has no README; use its Makefiles and source entrypoints as the source of truth.
- Phase 1/2 are older TCP cloud-drive implementations with separate `client/` and `server/` Makefiles.

## Build Commands
- Phase 3 server: `cd phase3-mysql/server && make`
- Phase 3 client: `cd phase3-mysql/client && make`
- Phase 3 binaries are `phase3-mysql/server/bin/cloud_drive_server` and `phase3-mysql/client/bin/cloud_drive_client`.
- Phase 1/2 build from each `client/` or `server/` directory with `make`; `make clean` and `make rebuild` are available there.
- Phase 2 server `make` runs `sudo chown root:root server` and `sudo chmod u+s server` for `/etc/shadow` auth; avoid running it casually without expecting sudo.

## Phase 3 Runtime Notes
- Server default config path is `config/server.ini`; client default is `config/client.ini`. Both also try fallback paths like `server/config/server.ini` or `client/config/client.ini` when run from `phase3-mysql/`.
- Phase 3 server needs MySQL client development tooling at build time because the Makefile calls `mysql_config`.
- Phase 3 server connects to MySQL from `server/config/server.ini` and calls `db_ensure_schema()` at startup before listening.
- Server startup forks: parent waits and child runs the epoll network process. Stop by sending `SIGUSR1` to the parent process.
- Each Phase 3 worker thread owns one MySQL connection.

## Current Implementation Gaps
- In Phase 3, account packets are routed to auth handlers, but file/directory handlers (`pwd`, `cd`, `ls`, `upload`, `download`, etc.) currently return "not implemented".
- `phase3-mysql/server/src/infra/db/db_connection.c` contains the auto-created schema; verify it before relying on files metadata behavior.

## Style And Layout
- Phase 3 code is C11 with `-Wall -Wextra -Werror -std=c11 -MMD -MP`.
- Phase 3 server includes are rooted at `server/include/`; client includes are rooted at `client/include/`.
- Third-party `inih` is vendored under each Phase 3 side's `third_party/inih`.
