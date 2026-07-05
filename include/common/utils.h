#pragma once
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

/**
 * elegant fgets
 *
 * read most size bytes from stream to s, no newline.
 */
char* s_fgets(char* s, int size, FILE* fp);
// read from stdin
char* s_gets(char* s, int size);

/**
 * trim two-end blink space
 */
char* trim(char* s);

/**
 * get_executable_dir
 *
 * get current process dir
 */
int get_executable_dir(char* dir, size_t size);

/**
 * get_parent_dir
 *
 * get parent dir of path
 */
int get_parent_dir(const char* path, char* dir, size_t size);

/**
 * get_project_dir
 *
 * get the project root dir
 */
int get_project_dir(char* path, size_t size);

/**
 * join_path
 *
 * if path is absolute path, just copy it, otherwise, dst = base_dir/path
 */
int join_path(char* dst, size_t dst_size, const char* base_dir, const char* path);

/**
 * mkdir_p
 *
 * recursively create dir like `mkdir -p`.
 */
int mkdir_p(const char *path, mode_t mode);

// Get the base name of path.
int utils_get_base_name(const char* path, char* base_name, int len);

// Expand a leading '~' to the current user's HOME.
int utils_expand_local_path(const char* path, char* out, size_t out_size);


/**
 * utils_format_current_time
 *
 * format current time to dest. the format is: %Y-%m-%d %H:%M:%S
 */
void utils_format_current_time(char *dest, size_t dest_size);

// ensure parent directory of the path exist, if not exist, mkdir recursively.
int ensure_parent_dir(const char *path);

/**
 * Safe read password
 *
 * Use tcgetattr and tcsetattr to modify termios attribute.
 */
int read_password(char *buf, size_t size);

/**
 * Normalizes a cd (change directory) path.
 *
 * Given the current working directory (@p cwd) and a target directory argument (@p cd_arg),
 * this function constructs a normalized absolute path. It resolves relative paths,
 * handles '.' and '..' components, and ensures the result fits in @p out.
 *
 * @param cwd      The current working directory (must be an absolute path).
 * @param cd_arg   The target directory argument (may be relative or absolute).
 * @param out      Output buffer where the normalized path will be written.
 * @param out_size Size of the output buffer.
 * @return 0 on success, or a non-zero error code on failure (e.g., buffer too small).
 */
int normalize_cd_path(const char* cwd, const char* cd_arg, char* out, size_t out_size);

// Compute SHA-256 of file_fd and write the 64-character hex string to file_hash.
int utils_get_file_sha256(int file_fd, char* file_hash, int size);

// Verify that s is sha256 hash or not 
int utils_is_valid_sha256_hex(const char *s);

// Write `len` length of data to file fd looply.
int write_n(int fd, const void *buf, size_t len);

