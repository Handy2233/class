#ifndef __PWM_H__
#define __PWM_H__

/**
 * @file pwm.h
 * @brief PWM 蜂鸣器控制接口。
 *
 * @details 本模块保留课程驱动协议控制有源蜂鸣器：
 * 1. 打开默认 PWM 设备 /dev/pwm。
 * 2. 写入 1 个控制字节。
 * 3. 关闭设备。
 *
 * 控制字节含义：
 * - 0：蜂鸣器不响。
 * - 1：蜂鸣器响。
 *
 * 默认设备路径可通过编译宏 PWM_DEV_PATH 覆盖，也可以在运行前设置：
 *
 *     export GEC6818_PWM_DEV="/dev/pwm"
 *
 * 若板端暴露 PWM 用户态接口，本模块还可以设置 PWM 频率，用于无源蜂鸣器
 * 变频输出。默认优先使用 GEC6818 板子的自定义接口：
 *
 *     /sys/devices/platform/pwm/pwm.2
 *
 * 该接口写入格式为：
 *
 *     频率Hz,占空比百分比
 *
 * 也可以通过环境变量切换到其他 PWM 文件：
 *
 *     export GEC6818_PWM_FILE="/sys/devices/platform/pwm/pwm.2"
 *
 * 如果板端内核暴露标准 Linux sysfs PWM，本模块还会回退使用 pwmchip0 的
 * pwm0 通道，可在运行前设置：
 *
 *     export GEC6818_PWM_CHIP="/sys/class/pwm/pwmchip0"
 *     export GEC6818_PWM_CHANNEL="0"
 */

/** @brief 变频蜂鸣器默认最低频率。 */
#define PWM_BEEP_MIN_HZ 1000

/** @brief 变频蜂鸣器默认最高频率。 */
#define PWM_BEEP_MAX_HZ 10000

/** @brief 变频蜂鸣器默认线性步长。 */
#define PWM_BEEP_STEP_HZ 1000

/**
 * @brief 控制蜂鸣器响/不响。
 *
 * @param[in] beep_value 蜂鸣器状态，0 表示不响，非 0 表示响。
 *
 * @retval 0 控制成功。
 * @retval -1 打开 PWM 设备失败。
 * @retval -2 写入 PWM 设备失败。
 */
int beep_ctl(char beep_value);

/**
 * @brief 按指定频率启动蜂鸣器 PWM 输出。
 *
 * @param[in] frequency_hz 输出频率，单位 Hz。
 *
 * @retval 0 设置成功。
 * @retval -1 频率参数非法。
 * @retval -2 sysfs PWM 通道不可用或导出失败。
 * @retval -3 写入 PWM period/duty_cycle/enable 失败。
 */
int pwm_beep_set_frequency_hz(int frequency_hz);

/**
 * @brief 关闭 sysfs PWM 蜂鸣器输出。
 *
 * @retval 0 关闭成功，或 sysfs PWM 通道不存在。
 * @retval -1 写入 enable 失败。
 */
int pwm_beep_stop(void);

#endif
