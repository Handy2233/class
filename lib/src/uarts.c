#include "uarts.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

/**
 * @file uarts.c
 * @brief UART 串口与 GY-39、Z-MQ-01 传感器通信实现。
 */

#define GY39_FRAME_HEAD 0x5A
#define GY39_COMMAND_HEAD 0xA5
#define ZMQ01_FRAME_HEAD 0xFF
#define ZMQ01_DEVICE_ADDR 0x01
#define ZMQ01_VALUE_HIGH_INDEX 2
#define ZMQ01_VALUE_LOW_INDEX 3
#define ZMQ01_CHECKSUM_INDEX 8

/**
 * @brief 获取 UART 超时控制用的毫秒时间戳。
 *
 * @return 当前时间，单位 ms。
 */
static long long uarts_now_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

/**
 * @brief 获取默认 UART 设备路径。
 *
 * @retval 非NULL UART 设备路径。
 *
 * @details 优先读取 GEC6818_UART_DEV，未设置时返回 UARTS_DEFAULT_DEV。
 */
const char *uarts_default_device(void)
{
    const char *device = getenv("GEC6818_UART_DEV");

    if(device != NULL && device[0] != '\0')
        return device;

    return UARTS_DEFAULT_DEV;
}

/**
 * @brief 获取默认 UART 波特率。
 *
 * @return 合法波特率；环境变量非法时返回 GY39_DEFAULT_BAUD。
 */
int uarts_default_baudrate(void)
{
    const char *text = getenv("GEC6818_UART_BAUD");
    char *endptr;
    long value;

    if(text == NULL || text[0] == '\0')
        return GY39_DEFAULT_BAUD;

    errno = 0;
    value = strtol(text, &endptr, 10);
    if(errno != 0 || endptr == text || *endptr != '\0' || value <= 0)
        return GY39_DEFAULT_BAUD;

    return (int)value;
}

/**
 * @brief 将整数波特率转换为 termios speed_t 常量。
 *
 * @param[in] baudrate 波特率数值。
 *
 * @retval 非0 termios 可用的 speed_t。
 * @retval 0 当前代码未支持该波特率。
 */
static speed_t uarts_baud_to_speed(int baudrate)
{
    switch(baudrate) {
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
    default:
        return 0;
    }
}

/**
 * @brief 打开并配置 UART 为原始非阻塞模式。
 *
 * @param[in] device 设备路径，NULL 或空串表示使用默认设备。
 * @param[in] baudrate 波特率，<=0 表示使用默认波特率。
 *
 * @retval >=0 打开的 UART 文件描述符。
 * @retval -1 打开设备失败。
 * @retval -2 波特率不支持。
 * @retval -3 termios 配置失败。
 */
int uarts_open(const char *device, int baudrate)
{
    struct termios options;
    speed_t speed;
    int fd;

    if(device == NULL || device[0] == '\0')
        device = uarts_default_device();

    if(baudrate <= 0)
        baudrate = uarts_default_baudrate();

    speed = uarts_baud_to_speed(baudrate);
    if(speed == 0) {
        LOG_ERROR("unsupported uart baudrate: %d", baudrate);
        return -2;
    }

    fd = open(device, O_RDWR | O_NONBLOCK | O_NOCTTY);
    if(fd < 0) {
        LOG_PERROR(device);
        return -1;
    }

    memset(&options, 0, sizeof(options));
    cfmakeraw(&options);
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);

    options.c_cflag |= CLOCAL | CREAD;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
#ifdef CRTSCTS
    options.c_cflag &= ~CRTSCTS;
#endif

    /*
     * 所有阻塞等待统一交给 uarts_read_exact_timeout() 处理。
     * 这里禁用 termios 自身的秒级等待，避免查询一帧数据时多等 1 秒。
     */
    options.c_cc[VTIME] = 0;
    options.c_cc[VMIN] = 0;

    tcflush(fd, TCIFLUSH);
    if(tcsetattr(fd, TCSANOW, &options) != 0) {
        LOG_PERROR("tcsetattr uart");
        close(fd);
        return -3;
    }

    return fd;
}

/**
 * @brief 关闭 UART 文件描述符。
 *
 * @param[in] fd UART 文件描述符，<0 时忽略。
 */
void uarts_close(int fd)
{
    if(fd >= 0)
        close(fd);
}

/**
 * @brief 尽量一次性写完整段 UART 数据。
 *
 * @param[in] fd UART 文件描述符。
 * @param[in] data 待写入数据。
 * @param[in] length 数据长度。
 *
 * @retval 0 写入成功。
 * @retval -1 参数无效。
 * @retval -2 写入失败或非阻塞设备暂时不可写。
 */
int uarts_write_all(int fd, const void *data, size_t length)
{
    const unsigned char *bytes = data;
    size_t offset = 0;

    if(fd < 0 || data == NULL)
        return -1;

    while(offset < length) {
        ssize_t written = write(fd, bytes + offset, length - offset);

        if(written > 0) {
            offset += (size_t)written;
            continue;
        }

        if(written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return -2;

        LOG_PERROR("write uart");
        return -2;
    }

    return 0;
}

/**
 * @brief 读取 UART 当前已经到达的数据。
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
int uarts_read_available(int fd, void *data, size_t length)
{
    ssize_t nread;

    if(fd < 0 || data == NULL)
        return -1;

    if(length == 0)
        return 0;

    nread = read(fd, data, length);
    if(nread > 0)
        return (int)nread;

    if(nread == 0)
        return 0;

    if(errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
        return 0;

    LOG_PERROR("read uart available");
    return -2;
}

/**
 * @brief 在总超时时间内读满指定长度。
 *
 * @param[in] fd UART 文件描述符。
 * @param[out] data 输出缓冲区。
 * @param[in] length 期望读取长度。
 * @param[in] timeout_ms 总超时时间，单位 ms。
 *
 * @retval 0 读取成功。
 * @retval -1 参数无效。
 * @retval -2 超时、select 失败或 read 失败。
 */
int uarts_read_exact_timeout(int fd, void *data, size_t length, int timeout_ms)
{
    unsigned char *bytes = data;
    size_t offset = 0;
    long long deadline;

    if(fd < 0 || data == NULL)
        return -1;

    if(timeout_ms < 0)
        timeout_ms = 0;

    deadline = uarts_now_ms() + timeout_ms;
    while(offset < length) {
        fd_set readfds;
        struct timeval timeout;
        long long remain = deadline - uarts_now_ms();
        ssize_t nread;
        int ready;

        if(remain <= 0)
            return -2;

        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        timeout.tv_sec = (time_t)(remain / 1000);
        timeout.tv_usec = (suseconds_t)((remain % 1000) * 1000);

        ready = select(fd + 1, &readfds, NULL, NULL, &timeout);
        if(ready < 0) {
            if(errno == EINTR)
                continue;
            LOG_PERROR("select uart");
            return -2;
        }

        if(ready == 0)
            return -2;

        nread = read(fd, bytes + offset, length - offset);
        if(nread < 0) {
            if(errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            LOG_PERROR("read uart");
            return -2;
        }

        if(nread == 0)
            continue;

        offset += (size_t)nread;
    }

    return 0;
}

/**
 * @brief 清空 UART 输入缓冲区。
 *
 * @param[in] fd UART 文件描述符。
 *
 * @retval 0 清空成功。
 * @retval -1 参数无效。
 * @retval -2 tcflush() 失败。
 */
int uarts_flush_input(int fd)
{
    if(fd < 0)
        return -1;

    if(tcflush(fd, TCIFLUSH) != 0) {
        LOG_PERROR("tcflush uart input");
        return -2;
    }

    return 0;
}

/**
 * @brief 计算 GY-39 命令校验和。
 *
 * @param[in] bytes 输入字节。
 * @param[in] length 参与求和的字节数。
 *
 * @return 低 8 位累加和。
 */
static unsigned char gy39_checksum(const unsigned char *bytes, int length)
{
    unsigned int sum = 0;
    int i;

    for(i = 0; i < length; i++)
        sum += bytes[i];

    return (unsigned char)(sum & 0xff);
}

/**
 * @brief 发送 GY-39 三字节查询命令。
 *
 * @param[in] fd UART 文件描述符。
 * @param[in] command GY-39 命令字。
 *
 * @retval 0 发送成功。
 * @retval <0 uarts_write_all() 返回的错误。
 */
int gy39_send_command(int fd, unsigned char command)
{
    unsigned char bytes[3];

    bytes[0] = GY39_COMMAND_HEAD;
    bytes[1] = command;
    bytes[2] = gy39_checksum(bytes, 2);

    return uarts_write_all(fd, bytes, sizeof(bytes));
}

/**
 * @brief 按大端格式读取 16 位无符号数。
 */
static unsigned int gy39_u16_be(const unsigned char *bytes)
{
    return ((unsigned int)bytes[0] << 8) | (unsigned int)bytes[1];
}

/**
 * @brief 按大端格式读取 32 位无符号数。
 */
static unsigned int gy39_u32_be(const unsigned char *bytes)
{
    return ((unsigned int)bytes[0] << 24) |
           ((unsigned int)bytes[1] << 16) |
           ((unsigned int)bytes[2] << 8) |
           (unsigned int)bytes[3];
}

/**
 * @brief 按大端格式读取 16 位有符号数。
 *
 * @return 转换成 int 后的有符号值。
 */
static int gy39_s16_be(const unsigned char *bytes)
{
    unsigned int value = gy39_u16_be(bytes);

    if(value & 0x8000U)
        return (int)value - 0x10000;

    return (int)value;
}

/**
 * @brief 读取一帧 GY-39 数据。
 *
 * @param[in] fd UART 文件描述符。
 * @param[out] frame 输出帧。
 * @param[in] timeout_ms 单次读取超时，单位 ms。
 *
 * @retval 0 读取成功。
 * @retval -1 参数无效。
 * @retval -2 等待超时或读取失败。
 * @retval -3 帧长度超过 GY39_MAX_DATA_LEN。
 */
int gy39_read_frame(int fd, gy39_frame_t *frame, int timeout_ms)
{
    unsigned char byte;

    if(fd < 0 || frame == NULL)
        return -1;

    memset(frame, 0, sizeof(*frame));

    while(1) {
        if(uarts_read_exact_timeout(fd, &byte, sizeof(byte),
                                    timeout_ms) != 0)
            return -2;

        if(byte != GY39_FRAME_HEAD)
            continue;

        if(uarts_read_exact_timeout(fd, &byte, sizeof(byte),
                                    timeout_ms) != 0)
            return -2;

        if(byte == GY39_FRAME_HEAD)
            break;
    }

    if(uarts_read_exact_timeout(fd, &frame->type, sizeof(frame->type),
                                timeout_ms) != 0)
        return -2;

    if(uarts_read_exact_timeout(fd, &frame->length, sizeof(frame->length),
                                timeout_ms) != 0)
        return -2;

    if(frame->length > GY39_MAX_DATA_LEN)
        return -3;

    if(uarts_read_exact_timeout(fd, frame->data, frame->length,
                                timeout_ms) != 0)
        return -2;

    return 0;
}

/**
 * @brief 解析 GY-39 光照或环境数据帧。
 *
 * @param[in] frame 原始帧。
 * @param[in,out] data 输出数据结构，只更新当前帧对应字段。
 *
 * @retval 0 解析成功。
 * @retval -1 参数无效。
 * @retval -2 帧类型或长度不符合当前支持格式。
 */
int gy39_parse_frame(const gy39_frame_t *frame, gy39_data_t *data)
{
    if(frame == NULL || data == NULL)
        return -1;

    if(frame->type == GY39_FRAME_LUX && frame->length == 4) {
        data->lux_centilux = gy39_u32_be(frame->data);
        data->has_lux = 1;
        return 0;
    }

    if(frame->type == GY39_FRAME_ENV && frame->length == 10) {
        data->temperature_centideg = gy39_s16_be(&frame->data[0]);
        data->pressure_centipa = gy39_u32_be(&frame->data[2]);
        data->humidity_centipercent = gy39_u16_be(&frame->data[6]);
        data->altitude_m = gy39_s16_be(&frame->data[8]);
        data->has_environment = 1;
        data->has_altitude = 1;
        return 0;
    }

    return -2;
}

/**
 * @brief 主动查询一次 GY-39 光照数据。
 *
 * @param[in] fd UART 文件描述符。
 * @param[in,out] data 输出数据结构。
 * @param[in] timeout_ms 单帧读取超时，单位 ms。
 *
 * @retval 0 查询成功。
 * @retval -1 参数无效。
 * @retval -2 查询命令发送失败。
 * @retval -3 未读取到有效光照帧。
 */
int gy39_query_lux(int fd, gy39_data_t *data, int timeout_ms)
{
    int i;

    if(fd < 0 || data == NULL)
        return -1;

    if(gy39_send_command(fd, GY39_CMD_QUERY_LUX) != 0)
        return -2;

    usleep(GY39_RESPONSE_DELAY_US);

    for(i = 0; i < 3; i++) {
        gy39_frame_t frame;

        if(gy39_read_frame(fd, &frame, timeout_ms) != 0)
            continue;

        if(frame.type == GY39_FRAME_LUX &&
           gy39_parse_frame(&frame, data) == 0)
            return 0;
    }

    return -3;
}

/**
 * @brief 主动查询一次 GY-39 环境数据。
 *
 * @param[in] fd UART 文件描述符。
 * @param[in,out] data 输出数据结构。
 * @param[in] timeout_ms 单帧读取超时，单位 ms。
 *
 * @retval 0 查询成功。
 * @retval -1 参数无效。
 * @retval -2 查询命令发送失败。
 * @retval -3 未读取到有效环境帧。
 */
int gy39_query_environment(int fd, gy39_data_t *data, int timeout_ms)
{
    int i;

    if(fd < 0 || data == NULL)
        return -1;

    if(gy39_send_command(fd, GY39_CMD_QUERY_ENV) != 0)
        return -2;

    usleep(GY39_RESPONSE_DELAY_US);

    for(i = 0; i < 3; i++) {
        gy39_frame_t frame;

        if(gy39_read_frame(fd, &frame, timeout_ms) != 0)
            continue;

        if(frame.type == GY39_FRAME_ENV &&
           gy39_parse_frame(&frame, data) == 0)
            return 0;
    }

    return -3;
}

/**
 * @brief 同步读取一轮 GY-39 光照和环境数据。
 *
 * @param[in] fd UART 文件描述符。
 * @param[in,out] data 输出数据结构。
 * @param[in] timeout_ms 单帧读取超时，单位 ms。
 *
 * @retval 0 至少读取到一种有效数据。
 * @retval -1 参数无效。
 * @retval -3 未读取到有效数据。
 *
 * @details UI 监测页使用非阻塞状态机，本函数保留给需要同步读取的调用方。
 */
int gy39_read_measurement(int fd, gy39_data_t *data, int timeout_ms)
{
    int got_any = 0;

    if(fd < 0 || data == NULL)
        return -1;

    uarts_flush_input(fd);

    if(gy39_query_lux(fd, data, timeout_ms) == 0)
        got_any = 1;

    uarts_flush_input(fd);

    if(gy39_query_environment(fd, data, timeout_ms) == 0)
        got_any = 1;

    return got_any ? 0 : -3;
}

/**
 * @brief 计算 Z-MQ-01 返回帧校验值。
 *
 * @param[in] bytes 9 字节命令或响应帧。
 *
 * @return 说明书定义的低 8 位求和后取补码校验值。
 */
static unsigned char zmq01_checksum(const unsigned char *bytes)
{
    unsigned int sum = 0;
    int i;

    for(i = 1; i < ZMQ01_CHECKSUM_INDEX; i++)
        sum += bytes[i];

    return (unsigned char)(0x100U - (sum & 0xffU));
}

/**
 * @brief 发送 Z-MQ-01 烟雾浓度读取命令。
 *
 * @param[in] fd UART 文件描述符。
 *
 * @retval 0 发送成功。
 * @retval <0 uarts_write_all() 返回的错误。
 */
int zmq01_send_read_command(int fd)
{
    unsigned char bytes[ZMQ01_FRAME_SIZE] = {
        ZMQ01_FRAME_HEAD,
        ZMQ01_DEVICE_ADDR,
        ZMQ01_CMD_READ_CONCENTRATION,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00
    };

    bytes[ZMQ01_CHECKSUM_INDEX] = zmq01_checksum(bytes);

    return uarts_write_all(fd, bytes, sizeof(bytes));
}

/**
 * @brief 解析 Z-MQ-01 9 字节响应帧。
 *
 * @param[in] raw 原始 9 字节响应。
 * @param[in,out] data 输出数据结构。
 *
 * @retval 0 解析成功。
 * @retval -1 参数无效。
 * @retval -2 帧头或命令字不匹配。
 * @retval -3 校验失败。
 */
int zmq01_parse_response(const unsigned char raw[ZMQ01_FRAME_SIZE],
                         zmq01_data_t *data)
{
    if(raw == NULL || data == NULL)
        return -1;

    if(raw[0] != ZMQ01_FRAME_HEAD ||
       raw[1] != ZMQ01_CMD_READ_CONCENTRATION)
        return -2;

    if(raw[ZMQ01_CHECKSUM_INDEX] != zmq01_checksum(raw))
        return -3;

    data->concentration =
        ((unsigned int)raw[ZMQ01_VALUE_HIGH_INDEX] << 8) |
        (unsigned int)raw[ZMQ01_VALUE_LOW_INDEX];
    data->has_concentration = 1;

    return 0;
}

/**
 * @brief 同步查询并读取一次 Z-MQ-01 烟雾浓度。
 *
 * @param[in] fd UART 文件描述符。
 * @param[in,out] data 输出数据结构。
 * @param[in] timeout_ms 读取超时，单位 ms。
 *
 * @retval 0 查询成功。
 * @retval -1 参数无效。
 * @retval -2 查询命令发送失败。
 * @retval -3 未读取到完整响应帧。
 * @retval -4 响应帧解析失败。
 *
 * @details UI 监测页使用非阻塞状态机，本函数保留给需要同步读取的调用方。
 */
int zmq01_read_measurement(int fd, zmq01_data_t *data, int timeout_ms)
{
    unsigned char raw[ZMQ01_FRAME_SIZE];

    if(fd < 0 || data == NULL)
        return -1;

    uarts_flush_input(fd);

    if(zmq01_send_read_command(fd) != 0)
        return -2;

    memset(raw, 0, sizeof(raw));
    if(uarts_read_exact_timeout(fd, raw, sizeof(raw), timeout_ms) != 0)
        return -3;

    if(zmq01_parse_response(raw, data) != 0)
        return -4;

    return 0;
}
