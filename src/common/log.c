#include "common/log.h"

#include <stdio.h>
#include <strings.h>
#include <pthread.h>
#include <stdarg.h>
#include <time.h>

typedef struct log_state_s {
    int level;
    FILE* fp;
    pthread_mutex_t lock;
    int init_done;
} log_state_t;

// global unique instance
static log_state_t s_log = {0};

static int parse_log_level(const char *level_str) {
    if (strcasecmp(level_str, "DEBUG") == 0) return LOG_LEVEL_DEBUG;
    if (strcasecmp(level_str, "INFO") == 0)  return LOG_LEVEL_INFO;
    if (strcasecmp(level_str, "WARN") == 0)  return LOG_LEVEL_WARN;
    if (strcasecmp(level_str, "ERROR") == 0) return LOG_LEVEL_ERROR;

    // default:
    return LOG_LEVEL_INFO;
}

static const char* log_level_to_str(int level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

int log_init(const char *level_str, const char *log_file) {
    if (s_log.init_done) {
        return 0;
    }

    int ret = 0;

    pthread_mutex_init(&s_log.lock, NULL);

    s_log.level = parse_log_level(level_str);
    s_log.fp = stdout;

    if (log_file != NULL && log_file[0] != '\0') {
        s_log.fp = fopen(log_file, "a");
        if (s_log.fp == NULL) {
            s_log.fp = stdout;
            ret = -1;
        }
    }

    s_log.init_done = 1;

    return ret;
}


void log_close(void) {
    if (!s_log.init_done) {
        return;
    }
    pthread_mutex_lock(&s_log.lock);

    if (s_log.fp  && s_log.fp != stdout) {
        fclose(s_log.fp);
    }

    s_log.fp = NULL;
    s_log.init_done = 0;

    pthread_mutex_unlock(&s_log.lock);
    pthread_mutex_destroy(&s_log.lock);
}

void log_write(int level, const char *file, int line, const char *func, const char *fmt, ...) {
    struct tm tm_info;

    // ignore lower log 
    if (level < s_log.level) {
        return;
    }

    time_t now = time(NULL);
    char time_str[64];

    // log text
    char log_msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(log_msg, sizeof(log_msg), fmt, args);
    va_end(args);

    pthread_mutex_lock(&s_log.lock);
    localtime_r(&now, &tm_info);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_info);

    /*
     * final log format:
     * [timestamp] [level] [filename:line func] message
     */
    fprintf(s_log.fp, "[%s] [%s] [%s:%d %s] %s\n",
            time_str,
            log_level_to_str(level),
            file,
            line,
            func,
            log_msg);

    fflush(s_log.fp);
    pthread_mutex_unlock(&s_log.lock);
}
