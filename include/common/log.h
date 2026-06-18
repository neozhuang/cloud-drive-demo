#pragma once

/*
 * Log Module
 *
 * The objective of log is not just print the info, more importantly,
 * - unify log level
 * - unify stdout format
 * - work at multi-thread
 * - friendly to debug, and position bug directly to file, line and function
 */ 

/*
 * log level:
 *
 * if current config log level set to INFO,
 * then DEBUG will not output, INFO/WARN/ERROR/ will print
 */
#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO 1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_ERROR 3

/*
 * log_init:
 *
 *   level_str - log level str, such as "debug" "info" "INFO"
 *   log_file  - log file, if null, degraded to stdout
 *
 * return:
 *   0  success
 *  -1  failed to open log file, degraded to stdout
 */
int log_init(const char* level_str, const char* log_file);

/*
 * log_close:
 */
void log_close(void);

/*
 * log_write:
 *
 * @parameter:
 *   level - log level
 *   file  - filename that invoked log func: __FILE__
 *   line  - line number that invoked log func: __LINE__
 *   func  - function name that invoked log func: __FUNCTION__ 
 *   fmt   - format string 
 *   ...   - virable args
 */
void log_write(int level, const char *file, int line, const char *func, const char *fmt, ...);

/*
 * log macro:
 *
 * @example:
 *   LOG_INFO("client connected fd=%d", fd);
 * will be expanded to:
 *   log_write(LOG_LEVEL_INFO, __FILE__, __LINE__, __FUNCTION__, ...);
 */
#define LOG_DEBUG(fmt, ...) do { log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__); } while (0)
#define LOG_INFO(fmt, ...)  do { log_write(LOG_LEVEL_INFO,  __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__); } while (0)
#define LOG_WARN(fmt, ...)  do { log_write(LOG_LEVEL_WARN,  __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__); } while (0)
#define LOG_ERROR(fmt, ...) do { log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__); } while (0)

