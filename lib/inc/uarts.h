#ifndef __UARTS_H__
#define __UARTS_H__

#include <stddef.h>

/**
 * @file uarts.h
 * @brief UART 串口与 GY-39、Z-MQ-01 传感器通信接口。
 *
 * @details 2-传感器模块.md 中的串口参数为 9600 bps，8N1，TTL 电平。
 * 本模块默认打开 UARTS_DEFAULT_DEV。若板端设备名不同，可在运行前设置：
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
 * - Byte4..: 数据区。本项目按板上模块实际返回读取到数据区为止；
 *   若模块额外返回帧尾校验字节，下一次查询前会清空输入缓冲。
 *
 * Z-MQ-01 烟雾传感器串口帧格式见《Z-MQ-01烟雾传感器使用说明书.pdf》：
 * - 查询命令：FF 01 86 00 00 00 00 00 79。
 * - 返回数据：FF 86 浓度高字节 浓度低字节 00 00 00 00 校验值。
 * - 校验值：地址/命令到数据字节求和后取低 8 位，再取二进制补码。
 */

/** @brief 当前默认 UART 设备节点。 */
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

/** @brief GY-39 查询命令发出后等待模块响应的默认时间，单位 us。 */
#define GY39_RESPONSE_DELAY_US 100000

/** @brief GY-39 单帧读取默认超时时间，单位 ms。 */
#define GY39_DEFAULT_READ_TIMEOUT_MS 500

/** @brief Z-MQ-01 模块默认串口波特率。 */
#define ZMQ01_DEFAULT_BAUD 9600

/** @brief Z-MQ-01 查询和返回帧长度，单位字节。 */
#define ZMQ01_FRAME_SIZE 9

/** @brief Z-MQ-01 读取传感器浓度值命令字。 */
#define ZMQ01_CMD_READ_CONCENTRATION 0x86

/** @brief Z-MQ-01 单帧读取默认超时时间，单位 ms。 */
#define ZMQ01_DEFAULT_READ_TIMEOUT_MS 500

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
 * @brief Z-MQ-01 烟雾传感器数据。
 *
 * @details concentration 为说明书返回的 16 位原始浓度值。PDF 未给出
 * ppm 等物理单位换算公式，因此这里保留模块直接返回值。
 */
typedef struct {
    int has_concentration;
    unsigned int concentration;
} zmq01_data_t;

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
 * @details 打开的文件描述符会设置为 O_NONBLOCK。需要等待完整帧的旧接口
 * 仍通过 select() 控制超时；事件循环可直接使用 uarts_read_available()
 * 只读取当前已经到达的数据。
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
 * @brief 读取当前 UART 中已经到达的数据，不等待新数据。
 *
 * @param[in] fd UART 文件描述符。
 * @param[out] data 输出缓冲区。
 * @param[in] length 最多读取字节数。
 *
 * @retval >0 实际读取字节数。
 * @retval 0 当前没有可读数据。
 * @retval -1 参数无效。
 * @retval -2 读取失败。
 */
int uarts_read_available(int fd, void *data, size_t length);

/**
 * @brief 在指定超时时间内读满一段 UART 数据。
 *
 * @details 本函数用 select() 控制整段读取的总超时时间，适合读取 GY-39
 * 这种固定长度应答帧。调用前应通过 uarts_open() 将串口配置为原始模式。
 *
 * @param[in] fd UART 文件描述符。
 * @param[out] data 输出缓冲区。
 * @param[in] length 期望读取的字节数。
 * @param[in] timeout_ms 整段读取的总超时时间，单位 ms。
 *
 * @retval 0 读取成功。
 * @retval -1 参数无效。
 * @retval -2 等待超时或读取失败。
 */
int uarts_read_exact_timeout(int fd,
                             void *data,
                             size_t length,
                             int timeout_ms);

/**
 * @brief 清空 UART 输入缓冲区。
 *
 * @param[in] fd UART 文件描述符。
 *
 * @retval 0 清空成功。
 * @retval -1 参数无效。
 * @retval -2 tcflush() 执行失败。
 */
int uarts_flush_input(int fd);

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
 * @param[in,out] data 输出传感器数据。函数只覆盖本次读取成功的字段，
 * 读取失败的字段保留原值。
 * @param[in] timeout_ms 每帧读取超时，单位 ms。
 *
 * @retval 0 至少读取到一类有效数据。
 * @retval -1 参数无效。
 * @retval -2 查询命令发送失败。
 * @retval -3 未读取到有效数据。
 */
int gy39_read_measurement(int fd, gy39_data_t *data, int timeout_ms);

/**
 * @brief 向 Z-MQ-01 发送读取浓度值命令。
 *
 * @param[in] fd UART 文件描述符。
 *
 * @retval 0 发送成功。
 * @retval <0 uarts_write_all() 返回的错误。
 */
int zmq01_send_read_command(int fd);

/**
 * @brief 解析并校验一帧 Z-MQ-01 返回数据。
 *
 * @param[in] raw 9 字节原始返回帧。
 * @param[in,out] data 输出数据。函数只更新烟雾浓度字段。
 *
 * @retval 0 解析成功。
 * @retval -1 参数无效。
 * @retval -2 帧头或命令字不匹配。
 * @retval -3 校验值错误。
 */
int zmq01_parse_response(const unsigned char raw[ZMQ01_FRAME_SIZE],
                         zmq01_data_t *data);

/**
 * @brief 主动查询并读取一次 Z-MQ-01 烟雾浓度数据。
 *
 * @param[in] fd UART 文件描述符。
 * @param[in,out] data 输出数据。
 * @param[in] timeout_ms 读取超时，单位 ms。
 *
 * @retval 0 查询成功。
 * @retval -1 参数无效。
 * @retval -2 查询命令发送失败。
 * @retval -3 未读取到完整返回帧。
 * @retval -4 返回帧校验失败。
 */
int zmq01_read_measurement(int fd, zmq01_data_t *data, int timeout_ms);

#endif
