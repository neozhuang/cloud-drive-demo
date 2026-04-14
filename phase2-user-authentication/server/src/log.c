#include "../include/log.h"
#include <stdarg.h>

static FILE *g_log_fp = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static void format_timestamp(char *buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", &tm_now);
}

int log_init(const char *log_path)
{
    pthread_mutex_lock(&g_log_mutex);
    if(g_log_fp != NULL)
    {
        pthread_mutex_unlock(&g_log_mutex);
        return 0;
    }

    g_log_fp = fopen(log_path, "a");
    if(g_log_fp == NULL)
    {
        pthread_mutex_unlock(&g_log_mutex);
        return -1;
    }

    setvbuf(g_log_fp, NULL, _IOLBF, 0);
    pthread_mutex_unlock(&g_log_mutex);
    return 0;
}

void log_close(void)
{
    pthread_mutex_lock(&g_log_mutex);
    if(g_log_fp != NULL)
    {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }
    pthread_mutex_unlock(&g_log_mutex);
}

const char *log_level_to_string(log_level_t level)
{
    switch(level)
    {
    case LOG_LEVEL_INFO:
        return "INFO";
    case LOG_LEVEL_WARN:
        return "WARN";
    case LOG_LEVEL_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

const char *cmd_type_to_string(cmd_type_t type)
{
    switch(type)
    {
    case PROMPT: return "PROMPT";
    case PWD: return "PWD";
    case CD: return "CD";
    case LS: return "LS";
    case LL: return "LL";
    case MKDIR: return "MKDIR";
    case RMDIR: return "RMDIR";
    case RM: return "RM";
    case PUTS: return "PUTS";
    case GETS: return "GETS";
    case TREE: return "TREE";
    case CAT: return "CAT";
    case LOGIN: return "LOGIN";
    case CLIENT_EXIT: return "CLIENT_EXIT";
    case NOTCMD: return "NOTCMD";
    default: return "UNKNOWN_CMD";
    }
}

void log_write(log_level_t level, const char *event, const char *fmt, ...)
{
    char timestamp[32] = {0};
    char message[2048] = {0};
    va_list ap;

    format_timestamp(timestamp, sizeof(timestamp));

    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    pthread_mutex_lock(&g_log_mutex);
    if(g_log_fp != NULL)
    {
        fprintf(g_log_fp,
                "[%s] [%s] [%s] tid=%lu %s\n",
                timestamp,
                log_level_to_string(level),
                event,
                (unsigned long)pthread_self(),
                message);
        fflush(g_log_fp);
    }
    pthread_mutex_unlock(&g_log_mutex);
}

void log_connection_event(const char *event,
                          int netfd,
                          const char *client_ip,
                          int client_port,
                          const char *username,
                          const char *detail)
{
    log_write(LOG_LEVEL_INFO,
              event,
              "netfd=%d client=%s:%d user=%s detail=\"%s\"",
              netfd,
              client_ip != NULL ? client_ip : "unknown",
              client_port,
              (username != NULL && username[0] != '\0') ? username : "anonymous",
              detail != NULL ? detail : "");
}

void log_request_event(int netfd,
                       const char *username,
                       const char *client_ip,
                       int client_port,
                       cmd_type_t request_type,
                       const char *request_arg)
{
    log_write(LOG_LEVEL_INFO,
              "REQUEST",
              "netfd=%d client=%s:%d user=%s cmd=%s arg=\"%s\"",
              netfd,
              client_ip != NULL ? client_ip : "unknown",
              client_port,
              (username != NULL && username[0] != '\0') ? username : "anonymous",
              cmd_type_to_string(request_type),
              request_arg != NULL ? request_arg : "");
}

void log_operation_event(int netfd,
                         const char *username,
                         const char *client_ip,
                         int client_port,
                         const char *cwd,
                         cmd_type_t operation,
                         const char *target,
                         int status,
                         const char *detail)
{
    log_write(status == 0 ? LOG_LEVEL_INFO : LOG_LEVEL_WARN,
              "OP",
              "netfd=%d client=%s:%d user=%s cwd=\"%s\" op=%s target=\"%s\" status=%d detail=\"%s\"",
              netfd,
              client_ip != NULL ? client_ip : "unknown",
              client_port,
              (username != NULL && username[0] != '\0') ? username : "anonymous",
              cwd != NULL ? cwd : "",
              cmd_type_to_string(operation),
              target != NULL ? target : "",
              status,
              detail != NULL ? detail : "");
}

void log_transfer_event(int netfd,
                        const char *username,
                        const char *client_ip,
                        int client_port,
                        const char *direction,
                        const char *filename,
                        off_t size,
                        int status,
                        long duration_ms,
                        const char *detail)
{
    log_write(status == 0 ? LOG_LEVEL_INFO : LOG_LEVEL_WARN,
              "TRANSFER",
              "netfd=%d client=%s:%d user=%s direction=%s file=\"%s\" size=%ld status=%d duration_ms=%ld detail=\"%s\"",
              netfd,
              client_ip != NULL ? client_ip : "unknown",
              client_port,
              (username != NULL && username[0] != '\0') ? username : "anonymous",
              direction != NULL ? direction : "unknown",
              filename != NULL ? filename : "",
              (long)size,
              status,
              duration_ms,
              detail != NULL ? detail : "");
}