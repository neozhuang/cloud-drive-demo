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
#include <time.h>
#include <termios.h>
#include <ctype.h>

#include <openssl/evp.h>
#include <openssl/sha.h>

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

int utils_get_base_name(const char* path, char* base_name, int len)
{
    char temp[PATH_MAX] = {0};
    char* base;
    size_t path_len;
    size_t base_len;

    if (path == NULL || base_name == NULL || len <= 0) {
        return -1;
    }
    path_len = strnlen(path, sizeof(temp));
    if (path_len == 0 || path_len == sizeof(temp)) {
        return -1;
    }
    memcpy(temp, path, path_len + 1U);
    base = basename(temp);
    if (base == NULL) {
        return -1;
    }
    base_len = strlen(base);
    if (base_len == 0 || base_len >= (size_t)len) {
        return -1;
    }
    memcpy(base_name, base, base_len + 1U);

    return 0;
}

int utils_expand_local_path(const char* path, char* out, size_t out_size)
{
    const char *home;
    int written;

    if (path[0] == '~' && (path[1] == '\0' || path[1] == '/')) {
        home = getenv("HOME");
        if (home == NULL || home[0] == '\0') {
            return -1;
        }

        written = snprintf(out, out_size, "%s%s", home, path + 1);
    } else {
        written = snprintf(out, out_size, "%s", path);
    }

    if (written < 0 || (size_t)written >= out_size) {
        return -1;
    }
    return 0;
}

void utils_format_current_time(char *dest, size_t dest_size)
{
    time_t now;
    struct tm *local_time;

    if (dest_size == 0) {
        return;
    }

    now = time(NULL);
    local_time = localtime(&now);
    if (local_time == NULL ||
        strftime(dest, dest_size, "%Y-%m-%d %H:%M:%S", local_time) == 0) {
        dest[0] = '\0';
    }
}

int ensure_parent_dir(const char *path)
{
    char dir[PATH_MAX];
    char *slash;

    if (path == NULL) {
        return -1;
    }

    if (snprintf(dir, sizeof(dir), "%s", path) >= (int)sizeof(dir)) {
        return -1;
    }

    // Create only the parent directory; the final path component is a file.
    slash = strrchr(dir, '/');
    if (slash == NULL) {
        return 0;
    }
    if (slash == dir) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }

    return mkdir_p(dir, 0755);
}

int read_password(char *buf, size_t size)
{
    struct termios old_term;
    struct termios new_term;
    size_t len = 0;
    int ch;

    if (buf == NULL || size == 0) {
        return -1;
    }

    if (tcgetattr(STDIN_FILENO, &old_term) != 0) {
        return -1;
    }

    new_term = old_term;
    new_term.c_lflag &= ~(ECHO | ICANON);

    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_term) != 0) {
        return -1;
    }

    while ((ch = getchar()) != '\n' && ch != EOF) {
        if (ch == 127 || ch == '\b') {
            if (len > 0) {
                len--;
                printf("\b \b");
                fflush(stdout);
            }
            continue;
        }

        if (len + 1 < size) {
            buf[len++] = (char)ch;
            putchar('*');
            fflush(stdout);
        }
    }

    buf[len] = '\0';
    putchar('\n');

    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);

    if (ch == EOF && len == 0) {
        return -1;
    }

    return 0;
}

int normalize_cd_path(const char* cwd, const char* cd_arg, char* out, size_t out_size) 
{
    char path[PATH_MAX];
    char *parts[PATH_MAX / 2];
    char *token;
    char *saveptr;
    int depth = 0;
    int written;

    if (cwd == NULL || cd_arg == NULL || out == NULL || out_size == 0) {
        return -1;
    }

    if (cd_arg[0] == '/') {
        written = snprintf(path, sizeof(path), "%s", cd_arg);
    } else if (strcmp(cwd, "/") == 0) {
        written = snprintf(path, sizeof(path), "/%s", cd_arg);
    } else {
        written = snprintf(path, sizeof(path), "%s/%s", cwd, cd_arg);
    }

    if (written < 0 || (size_t)written >= sizeof(path)) {
        return -1;
    }
    token = strtok_r(path, "/", &saveptr);
    while (token != NULL) {
        if (strcmp(token, ".") == 0) {
            /* ignore */
        } else if (strcmp(token, "..") == 0){
            if (depth > 0) {
                depth--;
            }
        } else {
            parts[depth++] = token;
        }
        token = strtok_r(NULL, "/", &saveptr);
    }

    if (depth == 0) {
        strcpy(out, "/");
        return 0;
    }

    out[0] = '\0';

    for (int i = 0; i < depth; ++i) {
        if (strlen(out) + 1 + strlen(parts[i]) + 1 > out_size) {
            return -1;
        }
        strcat(out, "/");
        strcat(out, parts[i]);
    }

    return 0;
}

int utils_get_file_sha256(int file_fd, char* file_hash, int size)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    unsigned char buffer[4096];
    ssize_t bytes_read;
    int required_size = SHA256_DIGEST_LENGTH * 2 + 1;

    if (file_fd < 0 || file_hash == NULL || size < required_size) {
        errno = EINVAL;
        return -1;
    }

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        fprintf(stderr, "EVP_MD_CTX_new failed\n");
        return -1;
    }

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1) {
        fprintf(stderr, "SHA256 digest failed\n");
        EVP_MD_CTX_free(ctx);
        return -1;
    }

    while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0) {
        if (EVP_DigestUpdate(ctx, buffer, bytes_read) != 1) {
            fprintf(stderr, "SHA256 digest failed\n");
            EVP_MD_CTX_free(ctx);
            return -1;
        }
    }

    if (bytes_read < 0) {
        perror("read");
        EVP_MD_CTX_free(ctx);
        return -1;
    }

    if (EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
        fprintf(stderr, "SHA256 digest failed\n");
        EVP_MD_CTX_free(ctx);
        return -1;
    }

    EVP_MD_CTX_free(ctx);

    for (unsigned int i = 0; i < digest_len; i++) {
        snprintf(file_hash + i * 2, size - (int)(i * 2), "%02x", digest[i]);
    }
    file_hash[digest_len * 2] = '\0';

    return 0;
}

int utils_is_valid_sha256_hex(const char *s)
{
    if (s == NULL || strlen(s) != 64) {
        return 0;
    }

    for (int i = 0; i < 64; i++) {
        if (!isxdigit((unsigned char)s[i])) {
            return 0;
        }
    }

    return 1;
}

int write_n(int fd, const void *buf, size_t len)
{
    const char *ptr = (const char *)buf;
    size_t total = 0;

    while (total < len) {
        ssize_t writed = write(fd, ptr + total, len - total);

        if (writed < 0) {
            // interpreted by signal, wait a moment and continue
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        total += (size_t)writed;
    }

    return 0;
}
