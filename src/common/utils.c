#include "common/utils.h"

#include <limits.h>
#include <linux/limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <libgen.h>

char* s_gets(char* s, int size)
{
    int ch;
    char* ret_val = fgets(s, size, stdin);
    if (ret_val) {
        char* find = strchr(ret_val, '\n');
        if (find) {
            *find = '\0';
        } else {
            while ((ch = getchar()) != '\n' && ch != EOF) {
                continue;
            }
        }
    }
    
    return ret_val;
}

char* s_fgets(char* s, int size, FILE* fp)
{
    int ch;
    char* ret_val = fgets(s, size, fp);
    if (ret_val) {
        char* find = strchr(ret_val, '\n');
        if (find) {
            *find = '\0';
        } else {
            while ((ch = fgetc(fp)) != '\n' && ch != EOF) {
                continue;
            }
        }
    }
    
    return ret_val;
}

char* trim(char* s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    char* end = s + strlen(s) - 1;
    while (end >= s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }
    return s;
}

/**
 * get_executable_dir
 *
 * get current process dir
 */
int get_executable_dir(char* dir, size_t size)
{
    ssize_t len = readlink("/proc/self/exe", dir, size - 1);
    if (len != -1) {
        dir[len] = '\0';
    }
    // exe_path == /home/zhuang/workspace/projects/cloud-drive-demo/bin/cdd-server

    char* slash = strrchr(dir, '/');
    if (slash == NULL) {
        return -1;
    }
    // exe_path == /home/zhuang/workspace/projects/cloud-drive-demo/bin
    *slash = '\0'; 
    return 0;
}

/**
 * get_parent_dir
 *
 * get parent dir of path
 */
int get_parent_dir(const char* path, char* dir, size_t size)
{
    char resolved[PATH_MAX];
    char* slash;

    if (realpath(path, resolved) == NULL) {
        perror("realpath");
        return -1;
    }

    slash = strrchr(resolved, '/');
    if (slash == NULL) {
        return -1;
    }

    // /home /etc /usr
    if (slash == resolved) {
        slash[1] = '\0';
    } else {
        *slash = '\0'; 
    }

    if (snprintf(dir, size, "%s", resolved) >= (int)size) {
        return -1; 
    }
    return 0;
}

int get_project_dir(char* path, size_t size)
{
    char cwd[PATH_MAX];
    char* slash;

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return -1;
    }

    slash = strrchr(cwd, '/');
    if (slash != NULL && strcmp(slash + 1, "bin") == 0) {
            *slash = '\0';
    }

    if (snprintf(path, size, "%s", cwd) >= (int)size) {
        return -1;
    }
    return 0;
}

/**
 * join_path
 *
 * if path is absolute path, just copy it, otherwise, dst = base_dir/path
 */
int join_path(char* dst, size_t dst_size, const char* base_dir, const char* path)
{
    int n;

    if (path[0] == '/') {
        n = snprintf(dst, dst_size, "%s", path);
    } else if (base_dir[0] == '\0') {
        n = snprintf(dst, dst_size, "%s", path);
    } else if (base_dir[strlen(base_dir) - 1] == '/') {
        n = snprintf(dst, dst_size, "%s%s", base_dir, path);
    } else {
        n = snprintf(dst, dst_size, "%s/%s", base_dir, path);
    }

    if (n < 0 || n >= (int)dst_size) {
        return -1;
    }
    return 0;
}

int mkdir_p(const char *path, mode_t mode)
{
    char tmp[PATH_MAX];
    size_t len;

    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    len = strlen(path);
    if (len >= sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    strcpy(tmp, path);
    if (len > 1 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (char *p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

int get_base_name(const char* path, char* base_name, int len)
{
    char temp[PATH_MAX];
    char* base;

    memset(temp, 0, sizeof(temp));
    if (path == NULL || base_name == NULL) {
        return -1;
    }

    memcpy(temp, path, strlen(path));
    base = basename(temp);
    if (base == NULL || (int)strlen(base) >= len) {
        return -1;
    }
    memcpy(base_name, base, len - 1);
    base_name[len - 1] = '\0';

    return 0;
}
