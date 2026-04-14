#ifndef __LOG_H__
#define __LOG_H__

#include "common.h"
#include "packet.h"

typedef enum {
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
} log_level_t;

int log_init(const char *log_path);
void log_close(void);
void log_write(log_level_t level, const char *event, const char *fmt, ...);

const char *log_level_to_string(log_level_t level);
const char *cmd_type_to_string(cmd_type_t type);

void log_connection_event(const char *event,
                          int netfd,
                          const char *client_ip,
                          int client_port,
                          const char *username,
                          const char *detail);

void log_request_event(int netfd,
                       const char *username,
                       const char *client_ip,
                       int client_port,
                       cmd_type_t request_type,
                       const char *request_arg);

void log_operation_event(int netfd,
                         const char *username,
                         const char *client_ip,
                         int client_port,
                         const char *cwd,
                         cmd_type_t operation,
                         const char *target,
                         int status,
                         const char *detail);

void log_transfer_event(int netfd,
                        const char *username,
                        const char *client_ip,
                        int client_port,
                        const char *direction,
                        const char *filename,
                        off_t size,
                        int status,
                        long duration_ms,
                        const char *detail);

#endif /* __LOG_H__ */
