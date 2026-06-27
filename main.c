#include "GEC6818.h"

#include <signal.h>
#include <sys/time.h>

/**
 * @file main.c
 * @brief GY-39 传感器 50ms 刷新验证程序。
 *
 * @details 用户已将 GY-39 模块连接至开发板 COM2。本程序默认打开
 * /dev/ttySAC1，按 9600 8N1 串口参数运行：
 * 1. 每 25ms 完成一次串口收发。
 * 2. 第 1 个 25ms 查询光照：A5 81 26。
 * 3. 第 2 个 25ms 查询温湿度/气压/海拔：A5 82 27。
 * 4. 光强和温湿度合起来算一次完整刷新，刷新周期为 50ms。
 * 5. 每次完整刷新后，将所有最近一次有效数据输出在同一行。
 */

#define GY39_STEP_INTERVAL_US 25000
#define GY39_FRAME_TIMEOUT_MS 6

static volatile sig_atomic_t stop_requested;

static void signal_handler(int signo)
{
    if(signo == SIGINT || signo == SIGTERM)
        stop_requested = 1;
}

static long long now_us(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}

static void sleep_until_us(long long target_us)
{
    while(!stop_requested) {
        long long delta = target_us - now_us();

        if(delta <= 0)
            break;

        if(delta > 20000)
            delta = 20000;

        usleep((useconds_t)delta);
    }
}

static void format_signed_2(char *out, size_t out_size, int value)
{
    int integer;
    int fraction;

    if(out == NULL || out_size == 0)
        return;

    if(value < 0) {
        integer = (-value) / 100;
        fraction = (-value) % 100;
        snprintf(out, out_size, "-%d.%02d", integer, fraction);
        return;
    }

    integer = value / 100;
    fraction = value % 100;
    snprintf(out, out_size, "%d.%02d", integer, fraction);
}

static void format_unsigned_2(char *out, size_t out_size, unsigned int value)
{
    if(out == NULL || out_size == 0)
        return;

    snprintf(out, out_size, "%u.%02u", value / 100, value % 100);
}

static void print_one_line(const gy39_data_t *data)
{
    char temp[24] = "N/A";
    char pressure[24] = "N/A";
    char humidity[24] = "N/A";
    char altitude[24] = "N/A";
    char lux[24] = "N/A";

    if(data == NULL)
        return;

    if(data->has_environment) {
        format_signed_2(temp, sizeof(temp), data->temperature_centideg);
        format_unsigned_2(pressure, sizeof(pressure), data->pressure_centipa);
        format_unsigned_2(humidity, sizeof(humidity),
                          data->humidity_centipercent);
        if(data->has_altitude)
            snprintf(altitude, sizeof(altitude), "%d", data->altitude_m);
    }

    if(data->has_lux)
        format_unsigned_2(lux, sizeof(lux), data->lux_centilux);

    LOG_INFO("temp=%sC pressure=%sPa humidity=%s%% altitude=%sm lux=%slux",
             temp, pressure, humidity, altitude, lux);
}

int main(void)
{
    struct sigaction action;
    const char *device = uarts_default_device();
    int baudrate = uarts_default_baudrate();
    gy39_data_t data;
    long long next_tick;
    int query_lux_next = 1;
    int fd;

    memset(&action, 0, sizeof(action));
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    memset(&data, 0, sizeof(data));

    fd = uarts_open(device, baudrate);
    if(fd < 0)
        return 1;

    LOG_INFO("GY-39 uart demo: device=%s baud=%d interval=25ms refresh=50ms",
             device, baudrate);

    next_tick = now_us();

    while(!stop_requested) {
        int ret;

        if(query_lux_next) {
            ret = gy39_query_lux(fd, &data, GY39_FRAME_TIMEOUT_MS);
            if(ret != 0)
                LOG_WARN("lux query failed");
        } else {
            ret = gy39_query_environment(fd, &data, GY39_FRAME_TIMEOUT_MS);
            if(ret != 0)
                LOG_WARN("environment query failed");

            print_one_line(&data);
        }

        query_lux_next = !query_lux_next;
        next_tick += GY39_STEP_INTERVAL_US;
        sleep_until_us(next_tick);

        if(now_us() - next_tick > GY39_STEP_INTERVAL_US)
            next_tick = now_us();
    }

    uarts_close(fd);
    return 0;
}
