#ifndef LOG_H
#define LOG_H

#include <errno.h>
#include <stdio.h>
#include <string.h>

/*
 * 轻量级日志头文件。
 *
 * 功能：
 * 1. 提供 DEBUG / INFO / WARN / ERROR / FATAL 五种日志输出宏。
 * 2. 每条日志自动带上日志级别、文件名、行号和函数名。
 * 3. 支持在编译时通过 LOG_LEVEL 过滤低优先级日志。
 * 4. LOG_PERROR(msg) 用于打印系统调用失败时的 errno 错误信息。
 *
 * 基本用法：
 *     LOG_INFO("lcd init ok");
 *     LOG_ERROR("x = %d is invalid", x);
 *
 * 系统调用失败时：
 *     int fd = open("/dev/fb0", O_RDWR);
 *     if(fd == -1) {
 *         LOG_PERROR("open /dev/fb0");
 *     }
 *
 * 编译时关闭低级别日志：
 *     arm-linux-gcc -DLOG_LEVEL=LOG_LEVEL_WARN ...
 *
 * 上面的命令会只保留 WARN / ERROR / FATAL 日志。
 */

/* 日志级别数字越小，优先级越低；LOG_LEVEL_NONE 表示关闭全部日志。 */
#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_ERROR 3
#define LOG_LEVEL_FATAL 4
#define LOG_LEVEL_NONE  5

/* 默认输出所有级别日志；用户可在编译命令中用 -DLOG_LEVEL=... 覆盖。 */
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_DEBUG
#endif

/* 默认输出到标准错误；用户可在包含本头文件前自定义 LOG_OUTPUT。 */
#ifndef LOG_OUTPUT
#define LOG_OUTPUT stderr
#endif

/* 从完整路径中取出文件名，避免日志里出现很长的编译路径。 */
static inline const char *log_basename(const char *path)
{
    const char *base = strrchr(path, '/');

    return base == NULL ? path : base + 1;
}

/* 内部基础输出宏，普通代码优先使用 LOG_DEBUG / LOG_INFO 等对外宏。 */
#define LOG_WRITE(level, fmt, ...)                                             \
    do {                                                                       \
        fprintf(LOG_OUTPUT, "[%s] %s:%d:%s(): " fmt "\n",                     \
                level, log_basename(__FILE__), __LINE__, __func__,             \
                ##__VA_ARGS__);                                                \
    } while (0)

/* 内部 errno 输出宏，普通代码优先使用 LOG_PERROR(msg)。 */
#define LOG_WRITE_ERRNO(level, msg, errnum)                                    \
    do {                                                                       \
        fprintf(LOG_OUTPUT, "[%s] %s:%d:%s(): %s: %s\n",                      \
                level, log_basename(__FILE__), __LINE__, __func__,             \
                msg, strerror(errnum));                                        \
    } while (0)

/* 调试信息：用于临时查看变量、流程，不建议长期保留大量输出。 */
#if LOG_LEVEL <= LOG_LEVEL_DEBUG
#define LOG_DEBUG(fmt, ...) LOG_WRITE("DEBUG", fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...) do { } while (0)
#endif

/* 普通信息：用于记录程序正常流程中的关键状态。 */
#if LOG_LEVEL <= LOG_LEVEL_INFO
#define LOG_INFO(fmt, ...) LOG_WRITE("INFO", fmt, ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...) do { } while (0)
#endif

/* 警告信息：程序还能继续运行，但出现了需要注意的情况。 */
#if LOG_LEVEL <= LOG_LEVEL_WARN
#define LOG_WARN(fmt, ...) LOG_WRITE("WARN", fmt, ##__VA_ARGS__)
#else
#define LOG_WARN(fmt, ...) do { } while (0)
#endif

/* 错误信息：当前操作失败。LOG_PERROR 会额外输出 errno 对应的原因。 */
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

/* 严重错误：通常表示程序已经无法按预期继续执行。 */
#if LOG_LEVEL <= LOG_LEVEL_FATAL
#define LOG_FATAL(fmt, ...) LOG_WRITE("FATAL", fmt, ##__VA_ARGS__)
#else
#define LOG_FATAL(fmt, ...) do { } while (0)
#endif

#endif
