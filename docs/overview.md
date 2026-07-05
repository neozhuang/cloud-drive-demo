# Cloud Drive Demo Overview

Cloud Drive Demo is a client-server file management system that provides a command-line interface for remote file operations. The project is designed to demonstrate the core capabilities of a cloud drive: connecting to a remote storage service, browsing directories, managing files, uploading and downloading data, recording server activity, and supporting reliable large-file transfer.

## What This Project Can Do

The client connects to the server and lets users operate on remote files through simple shell-like commands. The server receives each request, executes the corresponding file operation, and returns the result to the client.

Planned capabilities include:

- Remote directory navigation with commands such as `pwd`, `cd`, and `ls`.
- Remote directory and file management with commands such as `mkdir`, `rmdir`, and `rm`.
- File upload from client to server with `puts`.
- File download from server to client with `gets`.
- Multi-client handling through server-side worker threads.
- Server-side operation logging for connections, requests, and file changes.
- Runtime configuration for network settings, logging behavior, and transfer thresholds.
- Resumable upload and download when a transfer is interrupted.
- Optimized large-file transfer using platform-level mechanisms such as `mmap` and `sendfile`.

## Phase 1: Basic Cloud Drive Framework

The first phase focuses on building the core client-server workflow and implementing basic remote file commands.

### Client Features

The client is responsible for user interaction and request delivery. It will:

- Establish a connection with the server.
- Read commands from standard input.
- Parse each command into a command type and arguments.
- Validate command syntax before sending requests.
- Reject invalid input locally when possible.
- Send valid requests to the server.
- Display server responses to the user.

Supported commands in this phase include:

- `pwd`: show the current remote directory.
- `cd`: change the current remote directory.
- `ls`: list files in the current remote directory.
- `mkdir`: create a remote directory.
- `rmdir`: remove a remote directory.
- `rm`: remove a remote file.
- `puts`: upload a local file to the server.
- `gets`: download a remote file from the server.

### Server Features

The server is responsible for connection management, request handling, and file-system operations. It will:

- Start a listening service on a configured IP address and port.
- Accept client connection requests.
- Assign accepted client connections to worker threads.
- Receive parsed commands from clients.
- Execute the corresponding file operation.
- Return success, failure, or data responses to the client.

### Server Logging

The server will record important runtime activity, including:

- Client connection and disconnection time.
- Client request details.
- File operation records, such as upload, download, remove, and directory changes.
- Error information for failed requests or abnormal connections.

These logs help with debugging during development and provide operational visibility when the server is running.

### Configuration Files

The project will use configuration files to avoid hard-coding environment-specific values. Configuration can include:

- Server IP address.
- Server port.
- Database credentials, if database support is added.
- Log level.
- Large-file transfer threshold.

Configuration files make it possible to run the same code in different environments, such as local development, testing, and production. Sensitive configuration files can be excluded from version control when needed.

### Configurable Logging

The logging system should support multiple log levels, such as `DEBUG`, `INFO`, `WARN`, and `ERROR`.

During development, lower log levels such as `DEBUG` or `INFO` are useful because they provide detailed execution traces. In production, higher log levels such as `WARN` or `ERROR` are preferred because they reduce unnecessary output and avoid wasting server resources.

At startup, the program reads the configured log level and uses it to decide which messages should be printed. For example, if the configured level is `ERROR`, lower-priority messages such as `DEBUG` and `INFO` should be ignored.

## Phase 2: Reliable And Optimized Transfers

The second phase focuses on improving file-transfer reliability and performance, especially for large files or unstable network connections.

### Resumable Transfer

Upload and download operations should support resume behavior after interruption.

For downloads, if a client is downloading a 215 MB file and the connection stops after 16 MB, the next `gets` request should continue from the 16 MB position instead of starting again from the beginning.

For uploads, if a client is uploading a 215 MB file and the connection stops after 16 MB, the next `puts` request should continue from the uploaded 16 MB position instead of sending the whole file again.

This feature reduces wasted bandwidth and improves the user experience when transferring large files.

### Large-File Transfer Optimization

Large files should use more efficient transfer strategies than regular buffered reads and writes.

Planned optimizations include:

- On the client side, use `mmap` for files larger than a configured threshold.
- On the server side, use `sendfile` when sending large files to the client.
- Make the threshold configurable so different environments can tune performance behavior.

## Phase 3: Database Support

The third phase introduces database-backed metadata and account management. Instead of relying only on the physical file-system layout, the server can use database tables to describe users, directories, files, ownership, and transfer state.

### Database-Backed File Structure

Earlier phases may maintain a virtual directory forest in memory or on disk to isolate files between users and between directories owned by the same user. This model is intuitive, but it makes the server-side file organization more complicated as the system grows.

The purpose of the directory forest is to represent relationships:

- Which directories belong to which user.
- Which directories are children of other directories.
- Which files are contained in each directory.
- Where each logical file is stored on the server.

In this phase, these relationships should be moved into database tables. The database can store directory-to-directory relationships, directory-to-file relationships, file ownership, file metadata, and the real storage path of each file.

Each physical file can be stored using a content-based alias, such as its hash value. With this design, the server no longer needs to mirror every user's logical directory tree as real directories on disk. Files from all users and all logical directories can be stored in a common server storage directory, while the database describes how those files appear to each user.

### User Accounts And Authentication

Database support also makes it possible to manage application users independently from system accounts.

A user table can store information such as:

- Username.
- Password hash.
- Per-user random salt.
- User status.
- Creation and update timestamps.

During registration, the server should generate a random salt for the user, combine the salt with the original password, compute a secure password hash, and store only the hash and salt in the database.

During login, the server should look up the user by username, read the stored salt, hash the submitted password using the same algorithm, and compare the computed hash with the stored password hash. The raw password should never be stored.

Optional account features can include:

- User logout.
- User disable or lock status.
- Password reset or password change.

### Instant Upload By File Hash

The database can also support instant upload behavior.

Before uploading file content, the client can calculate the file hash and send it to the server. The server then checks whether a file with the same hash already exists in storage.

If the file already exists, the server only needs to create a new logical file record for the current user and directory. The client does not need to upload the same content again.

If the file does not exist, the server creates the necessary metadata and receives the file content normally.

This feature reduces duplicate storage and avoids unnecessary network transfer.

### Upload State Tracking

The server must avoid exposing incomplete files as valid user files. Several designs are possible:

- Add a transfer-status field to the file metadata table. New uploads start with an incomplete status and are marked complete only after the file content is fully received and verified.
- Use a separate upload-session table for in-progress uploads. After the upload succeeds, move or promote the record into the main file metadata table.
- Use another equivalent design, as long as incomplete uploads cannot be used as normal files.

This state tracking is especially important when combined with resumable upload and instant upload logic.

