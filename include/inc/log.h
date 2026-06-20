#ifndef LOG_H
#define LOG_H

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_ERROR 3
#define LOG_LEVEL_FATAL 4
#define LOG_LEVEL_NONE  5

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_DEBUG
#endif

#ifndef LOG_OUTPUT
#define LOG_OUTPUT stderr
#endif

static inline const char *log_basename(const char *path)
{
    const char *base = strrchr(path, '/');

    return base == NULL ? path : base + 1;
}

#define LOG_WRITE(level, fmt, ...)                                             \
    do {                                                                       \
        fprintf(LOG_OUTPUT, "[%s] %s:%d:%s(): " fmt "\n",                     \
                level, log_basename(__FILE__), __LINE__, __func__,             \
                ##__VA_ARGS__);                                                \
    } while (0)

#define LOG_WRITE_ERRNO(level, msg, errnum)                                    \
    do {                                                                       \
        fprintf(LOG_OUTPUT, "[%s] %s:%d:%s(): %s: %s\n",                      \
                level, log_basename(__FILE__), __LINE__, __func__,             \
                msg, strerror(errnum));                                        \
    } while (0)

#if LOG_LEVEL <= LOG_LEVEL_DEBUG
#define LOG_DEBUG(fmt, ...) LOG_WRITE("DEBUG", fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...) do { } while (0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_INFO
#define LOG_INFO(fmt, ...) LOG_WRITE("INFO", fmt, ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...) do { } while (0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_WARN
#define LOG_WARN(fmt, ...) LOG_WRITE("WARN", fmt, ##__VA_ARGS__)
#else
#define LOG_WARN(fmt, ...) do { } while (0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_ERROR
#define LOG_ERROR(fmt, ...) LOG_WRITE("ERROR", fmt, ##__VA_ARGS__)
#define LOG_PERROR(msg)                                                        \
    do {                                                                       \
        int log_saved_errno = errno;                                           \
        LOG_WRITE_ERRNO("ERROR", msg, log_saved_errno);                       \
    } while (0)
#else
#define LOG_ERROR(fmt, ...) do { } while (0)
#define LOG_PERROR(msg) do { } while (0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_FATAL
#define LOG_FATAL(fmt, ...) LOG_WRITE("FATAL", fmt, ##__VA_ARGS__)
#else
#define LOG_FATAL(fmt, ...) do { } while (0)
#endif

#endif
