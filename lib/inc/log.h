#ifndef LOG_H
#define LOG_H

#include <errno.h>
#include <stdio.h>
#include <string.h>

/**
 * @file log.h
 * @brief 轻量级日志宏。
 *
 * @details 本头文件只依赖 C 标准库和 errno，适合在 LCD、触摸、LED、
 * PWM、UART 等底层模块里直接包含。它提供 DEBUG / INFO / WARN /
 * ERROR / FATAL 五种日志级别，每条日志自动带上文件名、行号和函数名。
 *
 * 常规输出：
 *
 *     LOG_INFO("lcd init ok");
 *     LOG_ERROR("x = %d is invalid", x);
 *
 * 系统调用失败时：
 *
 *     int fd = open("/dev/fb0", O_RDWR);
 *     if(fd == -1)
 *         LOG_PERROR("open /dev/fb0");
 *
 * 编译时可通过 -DLOG_LEVEL=LOG_LEVEL_WARN 过滤 DEBUG/INFO 输出。
 * 如果需要把日志重定向到文件，可在包含本头文件前定义 LOG_OUTPUT。
 */

/** @brief 调试级日志，数字越小优先级越低。 */
#define LOG_LEVEL_DEBUG 0

/** @brief 普通流程日志。 */
#define LOG_LEVEL_INFO  1

/** @brief 警告日志，程序通常仍可继续运行。 */
#define LOG_LEVEL_WARN  2

/** @brief 错误日志，当前操作失败。 */
#define LOG_LEVEL_ERROR 3

/** @brief 严重错误日志，通常表示程序无法按预期继续。 */
#define LOG_LEVEL_FATAL 4

/** @brief 关闭全部日志输出。 */
#define LOG_LEVEL_NONE  5

/** @brief 默认输出所有级别日志；编译命令可用 -DLOG_LEVEL=... 覆盖。 */
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_DEBUG
#endif

/** @brief 默认输出到标准错误；包含本头文件前可自定义 LOG_OUTPUT。 */
#ifndef LOG_OUTPUT
#define LOG_OUTPUT stderr
#endif

/**
 * @brief 从完整路径中取出文件名。
 *
 * @param[in] path __FILE__ 或其他文件路径。
 *
 * @retval 非NULL 路径最后一级文件名；若 path 中没有 '/'，则返回 path。
 *
 * @details 编译器传入的 __FILE__ 可能是较长的绝对路径。日志里只打印
 * basename，便于在串口终端或屏幕输出中阅读。
 */
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
