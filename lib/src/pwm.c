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

/**
 * @brief 获取课程蜂鸣器字符设备路径。
 *
 * @retval 非NULL PWM 字符设备路径。
 *
 * @details 优先读取 GEC6818_PWM_DEV 环境变量，便于板端设备名变化时
 * 不重新编译程序；未设置时使用 PWM_DEV_PATH。
 */
static const char *pwm_device_path(void)
{
    const char *path = getenv("GEC6818_PWM_DEV");

    if(path != NULL && path[0] != '\0')
        return path;

    return PWM_DEV_PATH;
}

/**
 * @brief 获取标准 Linux sysfs PWM 芯片目录。
 *
 * @retval 非NULL pwmchip 目录路径。
 */
static const char *pwm_sysfs_chip_path(void)
{
    const char *path = getenv("GEC6818_PWM_CHIP");

    if(path != NULL && path[0] != '\0')
        return path;

    return PWM_SYSFS_CHIP_PATH;
}

/**
 * @brief 获取 GEC6818 平台自定义 PWM 控制文件路径。
 *
 * @retval 非NULL 平台 PWM 控制文件路径。
 *
 * @details 课程板镜像常见接口是 /sys/devices/platform/pwm/pwm.2，
 * 写入“频率,占空比”即可输出 PWM。该接口比标准 sysfs PWM 更直接，
 * 因此变频控制会优先使用它。
 */
static const char *pwm_platform_file_path(void)
{
    const char *path = getenv("GEC6818_PWM_FILE");

    if(path != NULL && path[0] != '\0')
        return path;

    return PWM_PLATFORM_FILE_PATH;
}

/**
 * @brief 解析标准 sysfs PWM 通道号。
 *
 * @retval >=0 PWM 通道号。
 * @retval -1 环境变量或默认配置不是合法的非负整数。
 */
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

/**
 * @brief 拼接 sysfs PWM 文件路径。
 *
 * @param[out] dst 输出路径缓冲区。
 * @param[in] dst_size 输出缓冲区大小。
 * @param[in] base 父目录路径。
 * @param[in] name 子文件名。
 *
 * @retval 0 拼接成功。
 * @retval -1 输出缓冲区不足。
 */
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

/**
 * @brief 向 sysfs 或平台 PWM 控制文件写入文本。
 *
 * @param[in] path 文件路径。
 * @param[in] text 要写入的文本，不自动追加换行。
 *
 * @retval 0 写入成功。
 * @retval -1 打开或写入失败。
 *
 * @details sysfs 属性文件通常要求一次写入完整字符串，因此这里检查
 * write() 的返回长度，避免只写入部分内容后误判成功。
 */
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

/**
 * @brief 判断平台自定义 PWM 文件是否可写。
 *
 * @retval 1 文件存在且当前进程有写权限。
 * @retval 0 文件不可用。
 */
static int pwm_platform_file_available(void)
{
    return access(pwm_platform_file_path(), W_OK) == 0;
}

/**
 * @brief 通过 GEC6818 平台自定义接口设置 PWM 输出。
 *
 * @param[in] frequency_hz 输出频率，<=0 表示沿用上次成功频率。
 * @param[in] duty_percent 占空比百分比，函数内部会夹到 [0, 100]。
 *
 * @retval 0 设置成功。
 * @retval -1 写入平台控制文件失败。
 */
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

/**
 * @brief 生成标准 sysfs PWM 通道目录。
 *
 * @param[out] dst 输出路径缓冲区。
 * @param[in] dst_size 输出缓冲区大小。
 * @param[in] channel PWM 通道号。
 *
 * @retval 0 生成成功。
 * @retval -1 路径过长。
 */
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

/**
 * @brief 导出标准 sysfs PWM 通道并等待目录出现。
 *
 * @param[in] channel PWM 通道号。
 *
 * @retval 0 通道已经存在或导出成功。
 * @retval -1 pwmchip 不存在、导出失败或等待超时。
 *
 * @details Linux sysfs PWM 写 export 后，pwmN 目录可能不是同步出现。
 * 这里短暂轮询，避免后续立即写 period/duty_cycle 时因为目录尚未创建
 * 而失败。
 */
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

/**
 * @brief 通过课程 PWM 字符设备控制蜂鸣器响/停。
 *
 * @param[in] beep_value 0 表示停止，非 0 表示响。
 *
 * @retval 0 控制成功。
 * @retval -1 打开 PWM 字符设备失败。
 * @retval -2 写入控制字节失败。
 */
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

/**
 * @brief 设置蜂鸣器 PWM 输出频率。
 *
 * @param[in] frequency_hz 频率，单位 Hz。
 *
 * @retval 0 设置成功。
 * @retval -1 频率非法或过高。
 * @retval -2 sysfs PWM 通道不可用。
 * @retval -3 写入 PWM 控制文件失败。
 *
 * @details 优先使用 GEC6818 平台自定义 PWM 文件；该路径不可用时，
 * 回退到标准 Linux sysfs PWM。
 */
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

/**
 * @brief 停止 sysfs 或平台 PWM 蜂鸣器输出。
 *
 * @retval 0 停止成功，或当前没有可停止的 sysfs PWM 通道。
 * @retval -1 写入停止命令失败。
 */
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
