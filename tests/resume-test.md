# Resume Upload Test

This document records how to test resumable upload for `puts`.

## 1. Prepare Test File

Create a large file for resumable upload:

```bash
dd if=/dev/urandom of=/tmp/resume.bin bs=1M count=1000
sha256sum /tmp/resume.bin
```

## 2. Test Resumable Upload

Start uploading the resume test file:

```text
puts /tmp/resume.bin
```

Kill the client while upload is still running:

```bash
kill -9 <client_pid>
```

Check the partial file size on the server side:

```bash
ls -lh <server_saved_path>/resume.bin
```

Expected result:

```text
The server-side file exists and its size is smaller than /tmp/resume.bin.
```

Restart the client, log in again, then upload the same file again:

```text
puts /tmp/resume.bin
```

After upload completes, calculate hashes again:

```bash
sha256sum /tmp/resume.bin
sha256sum <server_saved_path>/resume.bin
```

Expected result:

```text
The client file hash and server file hash are the same.
```

## 3. Optional Debug Logs

To make the resumable path easier to verify, temporarily print the resume offset on the client after receiving `CMD_PUTS_RESP`:

```c
printf("resume offset: %lu\n", file_offset);
```

On the server, print the existing file size before sending `CMD_PUTS_RESP`:

```c
printf("server existing size: %lu\n", existing_size);
```

Expected result:

```text
The first upload should use resume offset 0.
The second upload after interruption should use a non-zero resume offset.
```

## 4. Pass Criteria

Resumable upload passes if:

```text
The interrupted server-side file is partial.
The second upload resumes from a non-zero offset.
The final file hash matches between client and server.
```
