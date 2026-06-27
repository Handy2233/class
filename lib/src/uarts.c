#include "uarts.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

/**
 * @file uarts.c
 * @brief UART 串口与 GY-39 传感器通信实现。
 */

#define GY39_FRAME_HEAD 0x5A
#define GY39_COMMAND_HEAD 0xA5
#define GY39_READ_RETRY_FRAMES 2
#define GY39_TEMP_MIN_CENTIDEG (-4000)
#define GY39_TEMP_MAX_CENTIDEG 8500
#define GY39_PRESSURE_MIN_CENTIPA 3000000U
#define GY39_PRESSURE_MAX_CENTIPA 12000000U
#define GY39_HUMIDITY_MAX_CENTIPERCENT 10000U
#define GY39_LUX_MAX_CENTILUX 18800000U
#define GY39_ALTITUDE_MIN_M (-500)
#define GY39_ALTITUDE_MAX_M 9000

const char *uarts_default_device(void)
{
    const char *device = getenv("GEC6818_UART_DEV");

    if(device != NULL && device[0] != '\0')
        return device;

    return UARTS_DEFAULT_DEV;
}

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

    fd = open(device, O_RDWR);
    if(fd < 0) {
        LOG_PERROR(device);
        return -1;
    }

    memset(&options, 0, sizeof(options));
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

    options.c_cc[VMIN] = 1;
    options.c_cc[VTIME] = 10;

    tcflush(fd, TCIOFLUSH);
    if(tcsetattr(fd, TCSANOW, &options) != 0) {
        LOG_PERROR("tcsetattr uart");
        close(fd);
        return -3;
    }

    return fd;
}

void uarts_close(int fd)
{
    if(fd >= 0)
        close(fd);
}

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

        if(written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            usleep(1000);
            continue;
        }

        LOG_PERROR("write uart");
        return -2;
    }

    return 0;
}

static int uarts_read_byte_timeout(int fd,
                                   unsigned char *value,
                                   int timeout_ms)
{
    fd_set readfds;
    struct timeval timeout;
    ssize_t nread;
    int ret;

    if(fd < 0 || value == NULL)
        return -1;

    if(timeout_ms < 0)
        timeout_ms = 0;

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    ret = select(fd + 1, &readfds, NULL, NULL, &timeout);
    if(ret <= 0)
        return -1;

    nread = read(fd, value, 1);
    if(nread != 1)
        return -1;

    return 0;
}

static unsigned char gy39_checksum(const unsigned char *bytes, int length)
{
    unsigned int sum = 0;
    int i;

    for(i = 0; i < length; i++)
        sum += bytes[i];

    return (unsigned char)(sum & 0xff);
}

int gy39_send_command(int fd, unsigned char command)
{
    unsigned char bytes[3];

    bytes[0] = GY39_COMMAND_HEAD;
    bytes[1] = command;
    bytes[2] = gy39_checksum(bytes, 2);

    return uarts_write_all(fd, bytes, sizeof(bytes));
}

int gy39_read_frame(int fd, gy39_frame_t *frame, int timeout_ms)
{
    unsigned char raw[4 + GY39_MAX_DATA_LEN];
    unsigned char byte;
    int i;

    if(fd < 0 || frame == NULL)
        return -1;

    memset(frame, 0, sizeof(*frame));

    while(1) {
        if(uarts_read_byte_timeout(fd, &byte, timeout_ms) != 0)
            return -2;

        if(byte != GY39_FRAME_HEAD)
            continue;

        if(uarts_read_byte_timeout(fd, &byte, timeout_ms) != 0)
            return -2;

        if(byte == GY39_FRAME_HEAD)
            break;
    }

    raw[0] = GY39_FRAME_HEAD;
    raw[1] = GY39_FRAME_HEAD;

    if(uarts_read_byte_timeout(fd, &raw[2], timeout_ms) != 0)
        return -2;

    if(uarts_read_byte_timeout(fd, &raw[3], timeout_ms) != 0)
        return -2;

    if(raw[3] > GY39_MAX_DATA_LEN) {
        LOG_ERROR("gy39 frame length too large: %u", raw[3]);
        return -3;
    }

    for(i = 0; i < raw[3]; i++) {
        if(uarts_read_byte_timeout(fd, &raw[4 + i], timeout_ms) != 0)
            return -2;
    }

    frame->type = raw[2];
    frame->length = raw[3];
    memcpy(frame->data, &raw[4], raw[3]);
    return 0;
}

static unsigned int gy39_u16_be(const unsigned char *bytes)
{
    return ((unsigned int)bytes[0] << 8) | (unsigned int)bytes[1];
}

static unsigned int gy39_u32_be(const unsigned char *bytes)
{
    return ((unsigned int)bytes[0] << 24) |
           ((unsigned int)bytes[1] << 16) |
           ((unsigned int)bytes[2] << 8) |
           (unsigned int)bytes[3];
}

static int gy39_s16_be(const unsigned char *bytes)
{
    unsigned int value = gy39_u16_be(bytes);

    if(value & 0x8000U)
        return (int)value - 0x10000;

    return (int)value;
}

int gy39_parse_frame(const gy39_frame_t *frame, gy39_data_t *data)
{
    int temperature;
    unsigned int pressure;
    unsigned int humidity;
    int altitude;

    if(frame == NULL || data == NULL)
        return -1;

    if(frame->type == GY39_FRAME_LUX && frame->length == 4) {
        data->lux_centilux = gy39_u32_be(frame->data);
        if(data->lux_centilux > GY39_LUX_MAX_CENTILUX)
            return -2;

        data->has_lux = 1;
        return 0;
    }

    if(frame->type == GY39_FRAME_ENV && frame->length == 10) {
        temperature = gy39_s16_be(&frame->data[0]);
        pressure = gy39_u32_be(&frame->data[2]);
        humidity = gy39_u16_be(&frame->data[6]);
        altitude = gy39_s16_be(&frame->data[8]);

        if(temperature < GY39_TEMP_MIN_CENTIDEG ||
           temperature > GY39_TEMP_MAX_CENTIDEG ||
           pressure < GY39_PRESSURE_MIN_CENTIPA ||
           pressure > GY39_PRESSURE_MAX_CENTIPA ||
           humidity > GY39_HUMIDITY_MAX_CENTIPERCENT)
            return -2;

        data->temperature_centideg = temperature;
        data->pressure_centipa = pressure;
        data->humidity_centipercent = humidity;
        data->altitude_m = altitude;
        data->has_altitude = altitude >= GY39_ALTITUDE_MIN_M &&
                             altitude <= GY39_ALTITUDE_MAX_M;
        data->has_environment = 1;
        return 0;
    }

    if(frame->type == GY39_FRAME_IIC_ADDR && frame->length == 1)
        return 0;

    return -2;
}

static int gy39_query_type(int fd,
                           gy39_data_t *data,
                           unsigned char command,
                           unsigned char expected_type,
                           int timeout_ms)
{
    int i;

    if(fd < 0 || data == NULL)
        return -1;

    tcflush(fd, TCIFLUSH);

    if(gy39_send_command(fd, command) != 0)
        return -2;

    for(i = 0; i < GY39_READ_RETRY_FRAMES; i++) {
        gy39_frame_t frame;

        if(gy39_read_frame(fd, &frame, timeout_ms) != 0)
            continue;

        if(frame.type != expected_type)
            continue;

        if(gy39_parse_frame(&frame, data) == 0)
            return 0;
    }

    return -3;
}

int gy39_query_lux(int fd, gy39_data_t *data, int timeout_ms)
{
    return gy39_query_type(fd, data, GY39_CMD_QUERY_LUX,
                           GY39_FRAME_LUX, timeout_ms);
}

int gy39_query_environment(int fd, gy39_data_t *data, int timeout_ms)
{
    return gy39_query_type(fd, data, GY39_CMD_QUERY_ENV,
                           GY39_FRAME_ENV, timeout_ms);
}

int gy39_read_measurement(int fd, gy39_data_t *data, int timeout_ms)
{
    int got_any = 0;

    if(fd < 0 || data == NULL)
        return -1;

    memset(data, 0, sizeof(*data));

    if(gy39_query_lux(fd, data, timeout_ms) == 0)
        got_any = 1;

    if(gy39_query_environment(fd, data, timeout_ms) == 0)
        got_any = 1;

    return got_any ? 0 : -3;
}
