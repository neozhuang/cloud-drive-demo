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

/**
 * get_base_name
 *
 * get the base name of path
 */
int get_base_name(const char* path, char* base_name, int len);

