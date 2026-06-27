#include "pwm.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * @file pwm.c
 * @brief PWM 蜂鸣器控制实现。
 *
 * @details beep_ctl() 保持和课程代码一致的 open/write/close 调用模型。
 * 变频接口使用 Linux sysfs PWM，设置 period/duty_cycle/enable。
 */

#ifndef PWM_DEV_PATH
#define PWM_DEV_PATH "/dev/pwm"
#endif

#ifndef PWM_SYSFS_CHIP_PATH
#define PWM_SYSFS_CHIP_PATH "/sys/class/pwm/pwmchip0"
#endif

#ifndef PWM_SYSFS_CHANNEL
#define PWM_SYSFS_CHANNEL "0"
#endif

#ifndef PWM_PLATFORM_FILE_PATH
#define PWM_PLATFORM_FILE_PATH "/sys/devices/platform/pwm/pwm.2"
#endif

#define PWM_PATH_MAX 256
#define PWM_NSEC_PER_SEC 1000000000UL

static int pwm_last_frequency_hz = PWM_BEEP_MIN_HZ;

static const char *pwm_device_path(void)
{
    const char *path = getenv("GEC6818_PWM_DEV");

    if(path != NULL && path[0] != '\0')
        return path;

    return PWM_DEV_PATH;
}

static const char *pwm_sysfs_chip_path(void)
{
    const char *path = getenv("GEC6818_PWM_CHIP");

    if(path != NULL && path[0] != '\0')
        return path;

    return PWM_SYSFS_CHIP_PATH;
}

static const char *pwm_platform_file_path(void)
{
    const char *path = getenv("GEC6818_PWM_FILE");

    if(path != NULL && path[0] != '\0')
        return path;

    return PWM_PLATFORM_FILE_PATH;
}

static int pwm_sysfs_channel(void)
{
    const char *value = getenv("GEC6818_PWM_CHANNEL");
    char *endptr;
    long channel;

    if(value == NULL || value[0] == '\0')
        value = PWM_SYSFS_CHANNEL;

    errno = 0;
    channel = strtol(value, &endptr, 10);
    if(errno != 0 || endptr == value || *endptr != '\0' || channel < 0)
        return -1;

    return (int)channel;
}

static int pwm_make_path(char *dst, size_t dst_size,
                         const char *base, const char *name)
{
    int len = snprintf(dst, dst_size, "%s/%s", base, name);

    if(len < 0 || (size_t)len >= dst_size) {
        LOG_ERROR("pwm path too long: %s/%s", base, name);
        return -1;
    }

    return 0;
}

static int pwm_write_text(const char *path, const char *text)
{
    ssize_t written;
    size_t len;
    int fd;

    fd = open(path, O_WRONLY);
    if(fd < 0) {
        LOG_PERROR(path);
        return -1;
    }

    len = strlen(text);
    written = write(fd, text, len);
    close(fd);

    if(written < 0 || (size_t)written != len) {
        LOG_PERROR(path);
        return -1;
    }

    return 0;
}

static int pwm_platform_file_available(void)
{
    return access(pwm_platform_file_path(), W_OK) == 0;
}

static int pwm_platform_set(int frequency_hz, int duty_percent)
{
    char text[64];

    if(frequency_hz <= 0)
        frequency_hz = pwm_last_frequency_hz;

    if(duty_percent < 0)
        duty_percent = 0;
    if(duty_percent > 100)
        duty_percent = 100;

    snprintf(text, sizeof(text), "%d,%d", frequency_hz, duty_percent);
    if(pwm_write_text(pwm_platform_file_path(), text) != 0)
        return -1;

    pwm_last_frequency_hz = frequency_hz;
    return 0;
}

static int pwm_channel_dir(char *dst, size_t dst_size, int channel)
{
    int len = snprintf(dst, dst_size, "%s/pwm%d",
                       pwm_sysfs_chip_path(), channel);

    if(len < 0 || (size_t)len >= dst_size) {
        LOG_ERROR("pwm channel path too long");
        return -1;
    }

    return 0;
}

static int pwm_export_channel(int channel)
{
    char channel_dir[PWM_PATH_MAX];
    char export_path[PWM_PATH_MAX];
    char text[32];
    int i;
    int fd;

    if(pwm_channel_dir(channel_dir, sizeof(channel_dir), channel) != 0)
        return -1;

    if(access(channel_dir, F_OK) == 0)
        return 0;

    if(access(pwm_sysfs_chip_path(), F_OK) != 0) {
        LOG_WARN("sysfs pwm chip not found: %s", pwm_sysfs_chip_path());
        return -1;
    }

    if(pwm_make_path(export_path, sizeof(export_path),
                     pwm_sysfs_chip_path(), "export") != 0)
        return -1;

    fd = open(export_path, O_WRONLY);
    if(fd < 0) {
        LOG_PERROR(export_path);
        return -1;
    }

    snprintf(text, sizeof(text), "%d", channel);
    if(write(fd, text, strlen(text)) < 0 && errno != EBUSY) {
        LOG_PERROR(export_path);
        close(fd);
        return -1;
    }
    close(fd);

    for(i = 0; i < 50; i++) {
        if(access(channel_dir, F_OK) == 0)
            return 0;
        usleep(10000);
    }

    LOG_ERROR("sysfs pwm channel not ready: %s", channel_dir);
    return -1;
}

int beep_ctl(char beep_value)
{
    const char value = beep_value ? 1 : 0;
    const char *path = pwm_device_path();
    ssize_t written;
    int fd;

    fd = open(path, O_WRONLY);
    if(fd < 0) {
        LOG_PERROR("open " PWM_DEV_PATH);
        return -1;
    }

    written = write(fd, &value, 1);
    close(fd);

    if(written < 0) {
        LOG_PERROR("write " PWM_DEV_PATH);
        return -2;
    }

    return 0;
}

int pwm_beep_set_frequency_hz(int frequency_hz)
{
    char channel_dir[PWM_PATH_MAX];
    char path[PWM_PATH_MAX];
    char text[32];
    unsigned long period_ns;
    unsigned long duty_ns;
    int channel;

    if(frequency_hz <= 0)
        return -1;

    if(pwm_platform_file_available()) {
        if(pwm_platform_set(frequency_hz, 50) != 0)
            return -3;

        return 0;
    }

    channel = pwm_sysfs_channel();
    if(channel < 0) {
        LOG_ERROR("invalid sysfs pwm channel");
        return -2;
    }

    if(pwm_export_channel(channel) != 0)
        return -2;

    if(pwm_channel_dir(channel_dir, sizeof(channel_dir), channel) != 0)
        return -2;

    period_ns = PWM_NSEC_PER_SEC / (unsigned long)frequency_hz;
    if(period_ns < 2) {
        LOG_ERROR("pwm frequency too high: %d Hz", frequency_hz);
        return -1;
    }
    duty_ns = period_ns / 2;

    if(pwm_make_path(path, sizeof(path), channel_dir, "enable") != 0)
        return -3;
    if(pwm_write_text(path, "0") != 0)
        return -3;

    if(pwm_make_path(path, sizeof(path), channel_dir, "period") != 0)
        return -3;
    snprintf(text, sizeof(text), "%lu", period_ns);
    if(pwm_write_text(path, text) != 0)
        return -3;

    if(pwm_make_path(path, sizeof(path), channel_dir, "duty_cycle") != 0)
        return -3;
    snprintf(text, sizeof(text), "%lu", duty_ns);
    if(pwm_write_text(path, text) != 0)
        return -3;

    if(pwm_make_path(path, sizeof(path), channel_dir, "enable") != 0)
        return -3;
    if(pwm_write_text(path, "1") != 0)
        return -3;

    return 0;
}

int pwm_beep_stop(void)
{
    char channel_dir[PWM_PATH_MAX];
    char path[PWM_PATH_MAX];
    int channel;

    if(pwm_platform_file_available()) {
        if(pwm_platform_set(pwm_last_frequency_hz, 0) != 0)
            return -1;

        return 0;
    }

    channel = pwm_sysfs_channel();
    if(channel < 0)
        return 0;

    if(pwm_channel_dir(channel_dir, sizeof(channel_dir), channel) != 0)
        return 0;

    if(access(channel_dir, F_OK) != 0)
        return 0;

    if(pwm_make_path(path, sizeof(path), channel_dir, "enable") != 0)
        return -1;

    if(pwm_write_text(path, "0") != 0)
        return -1;

    return 0;
}
