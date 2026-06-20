# Phase 2 Resume

Phase 2 adds resumable upload/download and large-file transfer optimization on top of the Phase 1 cloud drive protocol. The implementation keeps the existing TCP + TLV packet model, but changes the `puts` and `gets` transfer flows so both sides can agree on a resume offset before file data is sent.

## Goals

- Resume interrupted uploads without retransmitting the whole local file.
- Resume interrupted downloads without overwriting the existing local partial file.
- Use `mmap` on the client upload path when the remaining file data is large.
- Use `sendfile` on the server download path to reduce user-space copying.
- Keep normal command handling and long file-transfer handling separated in the server.

## Protocol Changes

The protocol still uses the common TLV header:

```c
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t cmd_type;
    uint32_t status;
    uint32_t data_len;
} tlv_header_t;
```

Phase 2 uses these file-transfer commands:

- `CMD_PUTS_REQ`: client asks to upload a file and sends `file_info_payload_t`.
- `CMD_PUTS_RESP`: server replies with `resume_payload_t`, whose offset is the current server-side file size.
- `CMD_GETS_REQ`: client asks to download a remote file.
- `CMD_GETS_RESP`: server replies with `file_info_payload_t`, including file name and total file size.
- `CMD_RESUME_POS`: client tells the server the local download offset.
- `CMD_FILE_DATA`: carries one file data chunk.
- `CMD_FILE_END`: marks the end of file data.
- `CMD_ACK` / `CMD_ERROR`: confirms success or reports protocol / I/O failure.

The common payload structs are:

```c
typedef struct {
    char file_name[PATH_MAX];
    uint64_t file_size;
} file_info_payload_t;

typedef struct {
    uint64_t offset;
} resume_payload_t;
```

All `uint64_t` fields are converted with `host_to_net_u64` and `net_to_host_u64` because standard `htonl` / `ntohl` only handle 32-bit values.

`FILE_CHUNK_SIZE` is currently 4096 bytes. `FILE_OPTIMIZATION_THRESHOLD` is a compile-time macro set to 100 MB.

## Resumable Upload: `puts`

The upload resume point is decided by the server, because the server owns the partially uploaded remote file.

Flow:

1. The client opens the local file and gets the total file size with `fstat`.
2. The client extracts the base file name with `get_base_name` so local directory layout is not exposed to the server.
3. The client sends `CMD_PUTS_REQ` with `file_info_payload_t` containing the base file name and total file size.
4. The server resolves the target path under the logged-in user's virtual cwd and opens it with `O_CREAT | O_RDWR`.
5. The server calls `fstat` on the target file. The existing file size becomes the resume offset.
6. The server sends `CMD_PUTS_RESP` with `resume_payload_t(offset)`.
7. The client receives the offset, seeks the local file to that position with `lseek`, and sends only the remaining bytes.
8. The server seeks the target file to the same offset and appends received `CMD_FILE_DATA` payloads.
9. The client sends `CMD_FILE_END` after all remaining bytes are sent.
10. The server verifies that the received byte count equals `file_size - offset`, receives `CMD_FILE_END`, then replies with `CMD_ACK`.

If the connection is interrupted during upload, the partially written server-side file remains in place. The next `puts` request for the same file name resumes from that partial file size.

## Resumable Download: `gets`

The download resume point is decided by the client, because the client owns the partially downloaded local file.

Flow:

1. The client sends `CMD_GETS_REQ` with the requested remote file path.
2. The server resolves the path under the current user's root and opens the file read-only.
3. The server gets the total file size with `fstat` and sends `CMD_GETS_RESP` with `file_info_payload_t`.
4. The client creates `downloads/` if needed and checks `downloads/<file_name>` with `stat`.
5. If the local file exists, its size becomes the resume offset. Otherwise the offset is 0.
6. The client sends `CMD_RESUME_POS` with `resume_payload_t(offset)`.
7. The server starts sending from that offset and transfers only `file_size - offset` bytes.
8. The server sends `CMD_FILE_END` after the remaining bytes are sent.
9. The client appends the received chunks to the local file and sends `CMD_ACK` after receiving `CMD_FILE_END`.

The client opens the local download file with `O_CREAT | O_WRONLY | O_APPEND`, so resumed data is appended to the existing partial file.

## Large Upload Optimization: `mmap`

On the client upload path, small remaining data is sent with the normal `read` loop. When `file_size - file_offset > FILE_OPTIMIZATION_THRESHOLD`, the client uses `mmap`.

The resume offset may not be page-aligned, but `mmap` requires an aligned file offset. The client handles this by calculating:

```c
uint64_t map_offset = file_offset / page_size * page_size;
uint64_t offset_delta = file_offset - map_offset;
uint64_t map_len = file_size - map_offset;
char* map = mmap(NULL, map_len, PROT_READ, MAP_SHARED, file_fd, map_offset);
```

The mapped pointer starts at `map + offset_delta`, which corresponds to the actual resume position. The client then sends `CMD_FILE_DATA` packets from the mapped memory in `FILE_CHUNK_SIZE` chunks.

## Download Optimization: `sendfile`

The server download path uses `sendfile` to send file data directly from the file descriptor to the client socket.

Because each file chunk still needs a TLV header, the server first writes the packet header with `send_packet_header`:

```c
send_packet_header(client_fd, CMD_FILE_DATA, STATUS_OK, chunk_size);
sendfile_exact(client_fd, file_fd, &offset, chunk_size);
```

`sendfile_exact` loops until the requested chunk size has been sent. It handles `EINTR` and treats short sends as partial progress, so every `CMD_FILE_DATA` header matches exactly one payload of `chunk_size` bytes.

## Server Task Model

Normal commands and file-transfer commands are handled differently.

Normal commands such as `pwd`, `cd`, `ls`, `mkdir`, `rmdir`, and `rm` are received by the epoll loop, wrapped as `packet_task_t`, and submitted to the thread pool through `handle_packet_task`.

File-transfer commands need ordered, continuous socket I/O. Splitting every file data packet into independent thread-pool tasks would require sequence numbers and reordering logic. Phase 2 avoids that complexity by making one worker temporarily own the connection during a transfer.

For `puts` and `gets`:

1. The epoll loop receives the first request packet.
2. It creates a `transfer_task_t` containing the client fd, the first packet, and server context.
3. It removes the client fd from epoll with `EPOLL_CTL_DEL`.
4. A worker runs `handle_transfer_task` and performs the full upload/download protocol directly on that fd.
5. After the transfer handler returns, the fd is added back to epoll with `EPOLLIN | EPOLLRDHUP`.

This keeps the transfer protocol simple and ordered while preserving the task-based design for normal commands.

## Verification

The repository includes manual verification notes:

- `tests/resume-test.md`: verifies interrupted `puts` upload and non-zero resume offset.
- `tests/mmap-test.md`: verifies normal `read` upload path and large-file `mmap` upload path.

Typical upload resume checks:

1. Create a large local file.
2. Start `puts` and kill the client during transfer.
3. Confirm the server-side file is partial.
4. Run `puts` again.
5. Confirm the second upload resumes from a non-zero offset and the final hashes match.

Typical `mmap` checks:

1. Upload a file larger than `FILE_OPTIMIZATION_THRESHOLD`.
2. Attach `strace` to the client.
3. Confirm the large-file path shows `mmap` and `munmap` calls.
4. Confirm the uploaded file hash matches the original file hash.

## Current Limitations

- Resume validation is based on file size only. There is no hash or block checksum negotiation.
- If an existing partial file has the same name but different content, the implementation still resumes from its size.
- The local download file is always placed under `downloads/<file_name>`.
- `FILE_OPTIMIZATION_THRESHOLD` is a compile-time macro, not a runtime config option.
