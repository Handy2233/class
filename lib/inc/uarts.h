#ifndef __UARTS_H__
#define __UARTS_H__

#include <stddef.h>

/**
 * @file uarts.h
 * @brief UART 串口与 GY-39 传感器通信接口。
 *
 * @details 2-传感器模块.md 中的串口参数为 9600 bps，8N1，TTL 电平。
 * 本模块默认按开发板 COM2 打开 /dev/ttySAC1。若板端设备名不同，
 * 可在运行前设置：
 *
 *     export GEC6818_UART_DEV="/dev/ttySAC1"
 *     export GEC6818_UART_BAUD="9600"
 *
 * GY-39 串口帧格式：
 * - Byte0: 0x5A
 * - Byte1: 0x5A
 * - Byte2: 数据类型，0x15 为光照，0x45 为温度/气压/湿度/海拔。
 * - Byte3: 数据长度。
 * - Byte4..: 数据区。
 *
 * @note 当前板上模块实际返回没有末尾校验字节，因此读取逻辑按帧头、
 * 类型、长度和数据区解析，不强制等待校验字节。
 */

/** @brief GEC6818 开发板 COM2 默认设备节点。 */
#define UARTS_DEFAULT_DEV "/dev/ttySAC1"

/** @brief GY-39 模块默认串口波特率。 */
#define GY39_DEFAULT_BAUD 9600

/** @brief GY-39 最大数据区长度。 */
#define GY39_MAX_DATA_LEN 16

/** @brief GY-39 光照强度数据帧类型。 */
#define GY39_FRAME_LUX 0x15

/** @brief GY-39 温度、气压、湿度、海拔数据帧类型。 */
#define GY39_FRAME_ENV 0x45

/** @brief GY-39 IIC 地址数据帧类型。 */
#define GY39_FRAME_IIC_ADDR 0x55

/** @brief GY-39 查询光照强度命令，对应发送 A5 81 26。 */
#define GY39_CMD_QUERY_LUX 0x81

/** @brief GY-39 查询温度、气压、湿度、海拔命令，对应发送 A5 82 27。 */
#define GY39_CMD_QUERY_ENV 0x82

/**
 * @brief GY-39 原始数据帧。
 */
typedef struct {
    unsigned char type;
    unsigned char length;
    unsigned char data[GY39_MAX_DATA_LEN];
} gy39_frame_t;

/**
 * @brief GY-39 换算后的传感器数据。
 *
 * @details 为避免不同 C 库 printf 浮点支持差异，数值使用定点形式保存：
 * - lux_centilux 为 lux * 100。
 * - temperature_centideg 为 摄氏度 * 100。
 * - pressure_centipa 为 Pa * 100。
 * - humidity_centipercent 为 百分比 * 100。
 */
typedef struct {
    int has_lux;
    unsigned int lux_centilux;

    int has_environment;
    int temperature_centideg;
    unsigned int pressure_centipa;
    unsigned int humidity_centipercent;
    int has_altitude;
    int altitude_m;
} gy39_data_t;

/**
 * @brief 获取默认 UART 设备路径。
 *
 * @retval 非NULL 默认设备路径，优先返回 GEC6818_UART_DEV 环境变量。
 */
const char *uarts_default_device(void);

/**
 * @brief 获取默认 UART 波特率。
 *
 * @return 默认波特率，优先返回 GEC6818_UART_BAUD 环境变量。
 */
int uarts_default_baudrate(void);

/**
 * @brief 打开并配置 UART。
 *
 * @param[in] device 串口设备路径，NULL 表示使用 uarts_default_device()。
 * @param[in] baudrate 波特率，0 表示使用 uarts_default_baudrate()。
 *
 * @retval >=0 打开的串口文件描述符。
 * @retval -1 打开设备失败。
 * @retval -2 不支持的波特率。
 * @retval -3 termios 配置失败。
 */
int uarts_open(const char *device, int baudrate);

/**
 * @brief 关闭 UART 文件描述符。
 *
 * @param[in] fd uarts_open() 返回的文件描述符。
 */
void uarts_close(int fd);

/**
 * @brief 完整写入一段数据。
 *
 * @param[in] fd UART 文件描述符。
 * @param[in] data 待写入数据。
 * @param[in] length 数据长度。
 *
 * @retval 0 写入成功。
 * @retval -1 参数无效。
 * @retval -2 写入失败。
 */
int uarts_write_all(int fd, const void *data, size_t length);

/**
 * @brief 向 GY-39 发送 3 字节命令。
 *
 * @param[in] fd UART 文件描述符。
 * @param[in] command 命令字，例如 GY39_CMD_QUERY_ENV。
 *
 * @retval 0 发送成功。
 * @retval <0 uarts_write_all() 返回的错误。
 */
int gy39_send_command(int fd, unsigned char command);

/**
 * @brief 读取并校验一帧 GY-39 数据。
 *
 * @param[in] fd UART 文件描述符。
 * @param[out] frame 输出帧。
 * @param[in] timeout_ms 单次等待超时，单位 ms。
 *
 * @retval 0 读取成功。
 * @retval -1 参数无效。
 * @retval -2 等待超时或读取失败。
 * @retval -3 帧长度非法。
 * @retval -4 保留。
 */
int gy39_read_frame(int fd, gy39_frame_t *frame, int timeout_ms);

/**
 * @brief 将 GY-39 原始帧解析到数据结构。
 *
 * @param[in] frame 原始帧。
 * @param[in,out] data 输出数据。函数只更新当前帧对应字段。
 *
 * @retval 0 解析成功。
 * @retval -1 参数无效。
 * @retval -2 不支持的帧类型或长度不匹配。
 */
int gy39_parse_frame(const gy39_frame_t *frame, gy39_data_t *data);

/**
 * @brief 查询一次光照强度并更新数据结构。
 *
 * @param[in] fd UART 文件描述符。
 * @param[in,out] data 输出数据。函数只更新光照字段。
 * @param[in] timeout_ms 读取超时，单位 ms。
 *
 * @retval 0 查询成功。
 * @retval -1 参数无效。
 * @retval -2 查询命令发送失败。
 * @retval -3 未读取到有效光照帧。
 */
int gy39_query_lux(int fd, gy39_data_t *data, int timeout_ms);

/**
 * @brief 查询一次温度、气压、湿度、海拔并更新数据结构。
 *
 * @param[in] fd UART 文件描述符。
 * @param[in,out] data 输出数据。函数只更新环境字段。
 * @param[in] timeout_ms 读取超时，单位 ms。
 *
 * @retval 0 查询成功。
 * @retval -1 参数无效。
 * @retval -2 查询命令发送失败。
 * @retval -3 未读取到有效环境帧。
 */
int gy39_query_environment(int fd, gy39_data_t *data, int timeout_ms);

/**
 * @brief 主动查询并读取一次完整 GY-39 数据。
 *
 * @param[in] fd UART 文件描述符。
 * @param[out] data 输出传感器数据。
 * @param[in] timeout_ms 每帧读取超时，单位 ms。
 *
 * @retval 0 至少读取到一类有效数据。
 * @retval -1 参数无效。
 * @retval -2 查询命令发送失败。
 * @retval -3 未读取到有效数据。
 */
int gy39_read_measurement(int fd, gy39_data_t *data, int timeout_ms);

#endif
