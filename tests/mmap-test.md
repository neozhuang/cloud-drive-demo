# Mmap Upload Test

This document records how to test the normal `read` path and the large-file `mmap` path for `puts`.

## 1. Prepare Test Files

Create a small file to test the normal `read` path:

```bash
printf "hello\n" > /tmp/small.txt
sha256sum /tmp/small.txt
```

Create a large file. The size must be larger than `FILE_OPTIMIZATION_THRESHOLD`:

```bash
dd if=/dev/urandom of=/tmp/big.bin bs=1M count=100
sha256sum /tmp/big.bin
```

## 2. Test Small File Upload

Start the server, log in from the client, then run:

```text
puts /tmp/small.txt
```

After upload completes, calculate the hash of the file saved on the server side:

```bash
sha256sum <server_saved_path>/small.txt
```

Expected result:

```text
The client file hash and server file hash are the same.
```

This verifies the small-file `read` path.

## 3. Test Large File Mmap Upload

Start the server, log in from the client, then run:

```text
puts /tmp/big.bin
```

After upload completes, calculate the hash of the file saved on the server side:

```bash
sha256sum <server_saved_path>/big.bin
```

Expected result:

```text
The client file hash and server file hash are the same.
```

To confirm that the client really uses `mmap`, attach `strace` to the client process before running `puts`:

```bash
strace -e mmap,munmap,read,sendto,write -p <client_pid>
```

Expected result:

```text
The large-file upload shows mmap and munmap calls.
The large-file upload should not rely on repeated read calls from the uploaded file fd.
```

## 4. Pass Criteria

Small file upload passes if:

```text
The small file hash matches between client and server.
```

Large file mmap upload passes if:

```text
The large file hash matches between client and server.
strace shows mmap and munmap calls in the client.
```
