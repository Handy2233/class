#ifndef __LED_H__
#define __LED_H__

/**
 * @file led.h
 * @brief LED 控制接口。
 *
 * @details 本模块面向 GEC6818 Linux 用户态程序，默认优先使用
 * /dev/led_drv 字符设备驱动。若该设备不存在，则回退到
 * /sys/class/leds。默认设备路径可通过编译宏 LED_DRV_PATH 覆盖，
 * 默认使用课程驱动协议 write(fd, buf, 2)：buf[0] 为状态字节
 * 0/1，buf[1] 为 LED 灯号。默认灯号为 D7/D8/D9/D10。设备路径可
 * 在运行前设置环境变量：
 *
 *     export GEC6818_LED_DRV="/dev/led_drv"
 *
 * sysfs 后备模式下，如果板端 LED 名称不固定，可以设置：
 *
 *     export GEC6818_LED_NAMES="led0,led1"
 *
 * 或分别设置：
 *
 *     export GEC6818_LED0="led0"
 *     export GEC6818_LED1="led1"
 *
 * 初始化后使用 LED 下标控制，适合 UI 层只关心“第几个 LED”的场景。
 */

/** @brief 本模块最多管理的 LED 数量。 */
#define LED_MAX_COUNT 4

/**
 * @brief 初始化 LED 模块。
 *
 * @retval >0 初始化成功，可控制的 LED 数量。
 * @retval -1 未发现可用 LED 或无法打开默认驱动及 sysfs。
 * @retval -2 打开 /sys/class/leds 失败。
 * @retval -3 LED 名称配置过长。
 *
 * @details 调用成功后可以用 led_count() 获取数量。字符设备模式下
 * 默认返回课程驱动支持的 D7/D8/D9/D10 数量；sysfs 模式下返回
 * 扫描到的 LED 数量。
 */
int led_init(void);

/**
 * @brief 释放 LED 模块状态。
 *
 * @details 当前实现没有长期持有文件描述符，本函数主要用于清空缓存状态，
 * 方便重新初始化或程序退出时保持调用习惯统一。
 */
void led_uninit(void);

/**
 * @brief 获取已经注册的 LED 数量。
 *
 * @return LED 数量，范围 [0, LED_MAX_COUNT]。
 */
int led_count(void);

/**
 * @brief 获取指定 LED 的名称。
 *
 * @param[in] index LED 下标。
 *
 * @retval 非NULL LED 名称字符串。
 * @retval NULL index 无效或 LED 模块尚未初始化。
 */
const char *led_name(int index);

/**
 * @brief 设置指定 LED 亮灭。
 *
 * @param[in] index LED 下标。
 * @param[in] on 非 0 表示点亮，0 表示熄灭。
 *
 * @retval 0 设置成功。
 * @retval -1 index 无效。
 * @retval -2 字符设备或 sysfs trigger 操作失败。
 * @retval -3 无法写入 brightness。
 * @retval -4 /dev/led_drv 控制命令失败。
 */
int led_set(int index, int on);

/**
 * @brief 读取指定 LED 当前亮灭状态。
 *
 * @param[in] index LED 下标。
 * @param[out] on 输出状态，非 0 表示当前亮度大于 0。
 *
 * @retval 0 读取成功。
 * @retval -1 参数或 index 无效。
 * @retval -2 无法读取 brightness。字符设备模式下返回缓存状态。
 */
int led_get(int index, int *on);

/**
 * @brief 翻转指定 LED 状态。
 *
 * @param[in] index LED 下标。
 *
 * @retval 0 翻转成功。
 * @retval <0 led_get() 或 led_set() 返回的错误。
 */
int led_toggle(int index);


#endif
