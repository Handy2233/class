#include "GEC6818.h"
#include "ui_app_data.h"

#include <sys/time.h>
#include <time.h>

#define PIC_DIR "./pic"
#define ALBUM_BACK_X 20
#define ALBUM_BACK_Y 18
#define ALBUM_BACK_W 118
#define ALBUM_BACK_H 48
#define ALBUM_PAGE_PANEL_MIN_W 138
#define ALBUM_PAGE_PANEL_PAD_X 24
#define ALBUM_PAGE_PANEL_H 48
#define ALBUM_PAGE_PANEL_Y 18
#define ALBUM_SWIPE_MIN_DISTANCE 80
#define DEVICE_LED_COUNT 4
#define DEVICE_LED_CARD_X 54
#define DEVICE_LED_CARD_Y 136
#define DEVICE_LED_CARD_W 452
#define DEVICE_LED_CARD_H 230
#define DEVICE_BEEP_CARD_X 530
#define DEVICE_BEEP_CARD_Y 136
#define DEVICE_BEEP_CARD_W 216
#define DEVICE_BEEP_CARD_H 230
#define DEVICE_TOGGLE_W 94
#define DEVICE_TOGGLE_H 40
#define SENSOR_GY39_UART_DEV "/dev/ttySAC1"
#define SENSOR_SMOKE_UART_DEV "/dev/ttySAC2"
#define SENSOR_REFRESH_INTERVAL_MS 1000
#define UI_TOUCH_WAIT_MS 10
#define SENSOR_GY39_LUX_TIMEOUT_MS 200
#define SENSOR_GY39_ENV_TIMEOUT_MS GY39_DEFAULT_READ_TIMEOUT_MS
#define SENSOR_GY39_MIN_TIMEOUT_MS 50
#define SENSOR_GY39_MAX_TIMEOUT_MS 3000
#define SENSOR_SMOKE_TIMEOUT_MS 160
#define SENSOR_GY39_RX_BUF_SIZE 64
#define SENSOR_SMOKE_RX_BUF_SIZE 32
#define UI_GY39_FRAME_HEAD 0x5A
#define UI_ZMQ01_FRAME_HEAD 0xFF
#define SENSOR_TEMP_ALARM_THRESHOLD_C 30
#define SENSOR_SMOKE_ALARM_THRESHOLD_RAW 300
#define UI_CONFIG_PATH "./ui_app.conf"
#define ALARM_HISTORY_LOG_PATH "./alarm_history.log"
#define ALARM_HISTORY_FALLBACK_LOG_PATH "/root/alarm_history.log"
#define ALARM_HISTORY_TMP_LOG_PATH "/tmp/alarm_history.log"
#define ALARM_HISTORY_PANEL_X 54
#define ALARM_HISTORY_PANEL_Y 394
#define ALARM_HISTORY_PANEL_W 692
#define ALARM_HISTORY_PANEL_H 52
#define ALARM_HISTORY_PAGE_MAX 2
#define ALARM_LOG_LINE_MAX 192
#define ALARM_HISTORY_ROW_Y 288
#define ALARM_HISTORY_ROW_H 72
#define ALARM_HISTORY_DETAIL_MAX_W 628
#define ALARM_SOUND_TOGGLE_X 610
#define ALARM_SOUND_TOGGLE_Y 146

/**
 * @file ui_app.c
 * @brief LCD 触摸前端和后端外设整合实现。
 *
 * @details 本模块负责绘制页面、处理触摸跳转，并接入电子相册、LED、
 * 蜂鸣器、GY-39 环境数据和 Z-MQ-01 烟雾浓度。
 */

static int device_led_ready;
static int device_led_count;
static int device_led_state[DEVICE_LED_COUNT];
static int device_beep_state;
static unsigned int *ui_frame_pixels;
static const ui_screen_def_t *current_screen;

/**
 * @brief 传感器非阻塞轮询状态。
 *
 * @details Linux 用户态不能直接编写硬件中断服务函数，本应用通过
 * 非阻塞 UART + 主循环状态机实现事件驱动读取。每次 ui_sensor_poll()
 * 只推进一个很小的阶段，避免传感器等待阻塞触摸响应。
 */
typedef enum {
    /** @brief 空闲状态，等待下一次刷新周期到达。 */
    SENSOR_POLL_IDLE = 0,

    /** @brief 已发送 GY-39 光照查询命令，等待光照帧。 */
    SENSOR_POLL_GY39_LUX,

    /** @brief 已发送 GY-39 环境查询命令，等待温湿度等环境帧。 */
    SENSOR_POLL_GY39_ENV,

    /** @brief 已发送 Z-MQ-01 查询命令，等待烟雾浓度帧。 */
    SENSOR_POLL_SMOKE
} ui_sensor_poll_step_t;

/**
 * @brief 监测页传感器、阈值和报警状态。
 *
 * @details 该结构集中保存 UART 文件描述符、最新传感器值、报警阈值、
 * 蜂鸣器联动状态以及非阻塞收包缓冲。UI 绘制只读取本结构，串口状态机
 * 只更新本结构，避免不同页面散落保存传感器状态。
 */
typedef struct {
    /** @brief GY-39 UART 文件描述符，<0 表示不可用。 */
    int gy39_fd;

    /** @brief Z-MQ-01 UART 文件描述符，<0 表示不可用。 */
    int smoke_fd;

    /** @brief 最近一轮是否成功获得 GY-39 有效数据。 */
    int gy39_ok;

    /** @brief 最近一轮是否成功获得 Z-MQ-01 有效数据。 */
    int smoke_ok;

    /** @brief 温度报警阈值，单位为 0.01 摄氏度。 */
    int temperature_threshold_centideg;

    /** @brief 烟雾报警阈值，使用 Z-MQ-01 原始返回值。 */
    unsigned int smoke_threshold;

    /** @brief 当前是否处于温度报警。 */
    int temperature_alarm;

    /** @brief 当前是否处于烟雾报警。 */
    int smoke_alarm;

    /** @brief 当前是否有任意报警处于激活状态。 */
    int alarm_active;

    /** @brief 是否允许报警时自动开启蜂鸣器。 */
    int alarm_sound_enabled;

    /** @brief 当前是否由报警逻辑接管蜂鸣器。 */
    int alarm_beep_active;

    /** @brief 历史报警次数，启动时从日志文件恢复。 */
    unsigned int alarm_count;

    /** @brief 最新 GY-39 数据。 */
    gy39_data_t gy39;

    /** @brief 最新 Z-MQ-01 数据。 */
    zmq01_data_t smoke;

    /** @brief 下次启动刷新周期的时间点，单位 ms。 */
    long long next_refresh_ms;

    /** @brief 当前轮询阶段的超时时间点，单位 ms。 */
    long long poll_deadline_ms;

    /** @brief 当前传感器轮询阶段。 */
    ui_sensor_poll_step_t poll_step;

    /** @brief 本轮 GY-39 光照或环境帧是否至少成功一项。 */
    int gy39_cycle_ok;

    /** @brief GY-39 非阻塞接收缓冲区。 */
    unsigned char gy39_rx[SENSOR_GY39_RX_BUF_SIZE];

    /** @brief GY-39 接收缓冲区当前有效字节数。 */
    size_t gy39_rx_len;

    /** @brief Z-MQ-01 非阻塞接收缓冲区。 */
    unsigned char smoke_rx[SENSOR_SMOKE_RX_BUF_SIZE];

    /** @brief Z-MQ-01 接收缓冲区当前有效字节数。 */
    size_t smoke_rx_len;
} ui_sensor_state_t;

/**
 * @brief 页面显示用报警日志条目。
 *
 * @details 文件中每行会被解析成时间、事件名和详情三段。固定长度数组
 * 避免在刷新页面时动态分配内存。
 */
typedef struct {
    /** @brief 报警时间，格式为 YYYY-MM-DD HH:MM:SS。 */
    char time_text[24];

    /** @brief 报警事件名称，例如“温度报警”。 */
    char event_text[80];

    /** @brief 报警时的传感器值和阈值详情。 */
    char detail_text[96];
} ui_alarm_log_item_t;

static ui_alarm_log_item_t alarm_fallback_items[ALARM_HISTORY_PAGE_MAX];
static int alarm_fallback_count;

static ui_sensor_state_t sensor_state = {
    .gy39_fd = -1,
    .smoke_fd = -1,
    .temperature_threshold_centideg = SENSOR_TEMP_ALARM_THRESHOLD_C * 100,
    .smoke_threshold = SENSOR_SMOKE_ALARM_THRESHOLD_RAW,
    .alarm_sound_enabled = 1
};

static void ui_sensor_request_refresh(void);

/**
 * @brief 获取当前单调用途的毫秒时间戳。
 *
 * @return 当前时间，单位 ms。
 *
 * @details 用于 UI 刷新间隔和传感器轮询超时判断。这里使用 gettimeofday()
 * 是因为目标板课程环境通常都可用，精度也足够处理百毫秒级刷新。
 */
static long long ui_now_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

/**
 * @brief 读取环境变量，未设置时返回默认值。
 *
 * @param[in] name 环境变量名。
 * @param[in] fallback 默认字符串。
 *
 * @retval 非NULL 环境变量值或 fallback。
 */
static const char *ui_env_or_default(const char *name, const char *fallback)
{
    const char *value = getenv(name);

    if(value != NULL && value[0] != '\0')
        return value;

    return fallback;
}

/**
 * @brief 获取报警日志主路径。
 *
 * @retval 非NULL 日志文件路径。
 */
static const char *ui_alarm_log_path(void)
{
    return ui_env_or_default("GEC6818_ALARM_LOG", ALARM_HISTORY_LOG_PATH);
}

/**
 * @brief 按优先级返回报警日志候选路径。
 *
 * @param[in] index 候选路径序号。
 *
 * @retval 非NULL 候选日志路径。
 * @retval NULL index 超出候选范围。
 *
 * @details 主路径可通过环境变量覆盖；后两个路径用于板端工作目录不可写
 * 或程序从不同目录启动时，尽量保留报警记录。
 */
static const char *ui_alarm_log_candidate(int index)
{
    switch(index) {
    case 0:
        return ui_alarm_log_path();
    case 1:
        return ALARM_HISTORY_FALLBACK_LOG_PATH;
    case 2:
        return ALARM_HISTORY_TMP_LOG_PATH;
    default:
        return NULL;
    }
}

/**
 * @brief 获取 UI 配置文件路径。
 *
 * @retval 非NULL 配置文件路径。
 */
static const char *ui_config_path(void)
{
    return ui_env_or_default("GEC6818_UI_CONFIG", UI_CONFIG_PATH);
}

/**
 * @brief 读取并校验整数环境变量。
 *
 * @param[in] name 环境变量名。
 * @param[in] fallback 默认值。
 * @param[in] min_value 合法最小值。
 * @param[in] max_value 合法最大值。
 *
 * @return 合法解析值；未设置或非法时返回 fallback。
 */
static int ui_env_int_or_default(const char *name,
                                 int fallback,
                                 int min_value,
                                 int max_value)
{
    const char *value = getenv(name);
    char *endptr;
    long parsed;

    if(value == NULL || value[0] == '\0')
        return fallback;

    errno = 0;
    parsed = strtol(value, &endptr, 10);
    if(errno != 0 || endptr == value || *endptr != '\0' ||
       parsed < min_value || parsed > max_value) {
        LOG_WARN("invalid %s=%s, use %d", name, value, fallback);
        return fallback;
    }

    return (int)parsed;
}

/**
 * @brief 解析配置文件中的整数值。
 *
 * @param[in] value 等号右侧字符串。
 * @param[in] fallback 默认值。
 * @param[in] min_value 合法最小值。
 * @param[in] max_value 合法最大值。
 *
 * @return 合法解析值；字符串为空、格式非法或越界时返回 fallback。
 */
static int ui_parse_int_value(const char *value,
                              int fallback,
                              int min_value,
                              int max_value)
{
    char *endptr;
    long parsed;

    if(value == NULL || value[0] == '\0')
        return fallback;

    errno = 0;
    parsed = strtol(value, &endptr, 10);
    if(errno != 0 || endptr == value || *endptr != '\0' ||
       parsed < min_value || parsed > max_value)
        return fallback;

    return (int)parsed;
}

/**
 * @brief 去除字符串首尾空白字符。
 *
 * @param[in,out] text 待处理字符串。
 *
 * @retval 非NULL 裁剪后字符串起始位置，可能不等于传入指针。
 * @retval NULL text 为 NULL。
 *
 * @details 本函数会在原缓冲区尾部写入 '\0'，因此参数必须是可写缓冲区。
 */
static char *ui_trim_space(char *text)
{
    char *end;

    if(text == NULL)
        return NULL;

    while(*text != '\0' && isspace((unsigned char)*text))
        text++;

    end = text + strlen(text);
    while(end > text && isspace((unsigned char)end[-1]))
        end--;
    *end = '\0';

    return text;
}

/**
 * @brief 设置运行参数默认值。
 *
 * @details 默认值优先来自环境变量，环境变量不存在或非法时使用编译期常量。
 * 随后 ui_settings_load() 会用配置文件覆盖这些默认值。
 */
static void ui_settings_set_defaults(void)
{
    sensor_state.temperature_threshold_centideg =
        ui_env_int_or_default("GEC6818_TEMP_ALARM_C",
                              SENSOR_TEMP_ALARM_THRESHOLD_C,
                              -40,
                              125) * 100;
    sensor_state.smoke_threshold =
        (unsigned int)ui_env_int_or_default("GEC6818_SMOKE_ALARM_RAW",
                                            SENSOR_SMOKE_ALARM_THRESHOLD_RAW,
                                            0,
                                            65535);
    sensor_state.alarm_sound_enabled =
        ui_env_int_or_default("GEC6818_ALARM_SOUND", 1, 0, 1);
}

/**
 * @brief 保存 UI 设置到配置文件。
 *
 * @retval 0 保存成功。
 * @retval -1 打开配置文件失败。
 * @retval -2 关闭配置文件失败。
 *
 * @details 保存温度阈值、烟雾阈值和声音报警开关，解决掉电或程序重启后
 * 设置丢失的问题。
 */
static int ui_settings_save(void)
{
    FILE *fp = fopen(ui_config_path(), "w");

    if(fp == NULL) {
        LOG_PERROR(ui_config_path());
        return -1;
    }

    fprintf(fp, "temperature_alarm_c=%d\n",
            sensor_state.temperature_threshold_centideg / 100);
    fprintf(fp, "smoke_alarm_raw=%u\n", sensor_state.smoke_threshold);
    fprintf(fp, "alarm_sound=%d\n", sensor_state.alarm_sound_enabled ? 1 : 0);

    if(fclose(fp) != 0) {
        LOG_PERROR(ui_config_path());
        return -2;
    }

    return 0;
}

/**
 * @brief 从配置文件恢复 UI 设置。
 *
 * @details 先调用 ui_settings_set_defaults() 建立默认值；若配置文件不存在，
 * 会立即写出一份默认配置。存在配置文件时仅识别当前支持的 key，未知 key
 * 会被忽略，便于以后扩展配置项。
 */
static void ui_settings_load(void)
{
    FILE *fp;
    char line[128];
    int loaded = 0;

    ui_settings_set_defaults();

    fp = fopen(ui_config_path(), "r");
    if(fp == NULL) {
        ui_settings_save();
        return;
    }

    while(fgets(line, sizeof(line), fp) != NULL) {
        char *key;
        char *value;
        char *sep;

        key = ui_trim_space(line);
        if(key == NULL || key[0] == '\0' || key[0] == '#')
            continue;

        sep = strchr(key, '=');
        if(sep == NULL)
            continue;

        *sep = '\0';
        value = ui_trim_space(sep + 1);
        key = ui_trim_space(key);

        if(strcmp(key, "temperature_alarm_c") == 0) {
            sensor_state.temperature_threshold_centideg =
                ui_parse_int_value(value,
                                   SENSOR_TEMP_ALARM_THRESHOLD_C,
                                   -40,
                                   125) * 100;
            loaded = 1;
        } else if(strcmp(key, "smoke_alarm_raw") == 0) {
            sensor_state.smoke_threshold =
                (unsigned int)ui_parse_int_value(value,
                                                 SENSOR_SMOKE_ALARM_THRESHOLD_RAW,
                                                 0,
                                                 65535);
            loaded = 1;
        } else if(strcmp(key, "alarm_sound") == 0) {
            sensor_state.alarm_sound_enabled =
                ui_parse_int_value(value, 1, 0, 1);
            loaded = 1;
        }
    }

    fclose(fp);

    if(!loaded)
        ui_settings_save();
}

/**
 * @brief 分配 UI 离屏帧缓冲。
 *
 * @retval 0 分配成功。
 * @retval -1 内存不足，后续会退化为直接绘制。
 *
 * @details 页面先画到内存，再一次性 flush 到 framebuffer，可减少刷新时
 * 肉眼看到的整体闪烁。
 */
static int ui_frame_init(void)
{
    ui_frame_pixels = malloc(LCD_SIZE);
    if(ui_frame_pixels == NULL) {
        LOG_WARN("ui offscreen frame disabled");
        return -1;
    }

    return 0;
}

/**
 * @brief 释放 UI 离屏帧缓冲。
 */
static void ui_frame_cleanup(void)
{
    lcd_set_draw_buffer(NULL);
    free(ui_frame_pixels);
    ui_frame_pixels = NULL;
}

/**
 * @brief 开始一帧 UI 绘制。
 *
 * @details 如果离屏缓冲可用，则临时把 lcd.c 的绘制目标切到该缓冲。
 */
static void ui_frame_begin(void)
{
    if(ui_frame_pixels != NULL)
        lcd_set_draw_buffer(ui_frame_pixels);
}

/**
 * @brief 结束一帧 UI 绘制并刷新到 LCD。
 */
static void ui_frame_end(void)
{
    if(ui_frame_pixels == NULL)
        return;

    lcd_set_draw_buffer(NULL);
    if(lcd_flush_draw_buffer(ui_frame_pixels) != 0)
        LOG_ERROR("ui offscreen frame flush failed");
}

/**
 * @brief 填充矩形区域。
 *
 * @param[in] x 左上角 x 坐标。
 * @param[in] y 左上角 y 坐标。
 * @param[in] w 宽度。
 * @param[in] h 高度。
 * @param[in] color 填充颜色，格式为 0x00RRGGBB。
 */
static void ui_fill_rect(int x, int y, int w, int h, unsigned int color)
{
    int row;
    int col;

    for(row = 0; row < h; row++) {
        for(col = 0; col < w; col++) {
            lcd_show(x + col, y + row, color);
        }
    }
}

/**
 * @brief 按比例混合两个 RGB 颜色。
 *
 * @param[in] from 起始颜色。
 * @param[in] to 目标颜色。
 * @param[in] percent 混合比例，0 表示完全使用 from，100 表示完全使用 to。
 *
 * @return 混合后的 0x00RRGGBB 颜色。
 */
static unsigned int ui_rgb_mix(unsigned int from,
                               unsigned int to,
                               int percent)
{
    int from_r = (from >> 16) & 0xff;
    int from_g = (from >> 8) & 0xff;
    int from_b = from & 0xff;
    int to_r = (to >> 16) & 0xff;
    int to_g = (to >> 8) & 0xff;
    int to_b = to & 0xff;
    int r = from_r + (to_r - from_r) * percent / 100;
    int g = from_g + (to_g - from_g) * percent / 100;
    int b = from_b + (to_b - from_b) * percent / 100;

    return ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)b;
}

/**
 * @brief 判断圆角矩形局部坐标是否在可填充区域内。
 *
 * @param[in] px 局部 x 坐标。
 * @param[in] py 局部 y 坐标。
 * @param[in] w 矩形宽度。
 * @param[in] h 矩形高度。
 * @param[in] radius 圆角半径。
 *
 * @retval 1 坐标位于圆角矩形内部。
 * @retval 0 坐标位于圆角裁剪区域外。
 */
static int ui_round_contains(int px, int py, int w, int h, int radius)
{
    int cx = px;
    int cy = py;
    int dx;
    int dy;

    if(radius <= 0)
        return 1;

    if(px < radius)
        cx = radius;
    else if(px >= w - radius)
        cx = w - radius - 1;

    if(py < radius)
        cy = radius;
    else if(py >= h - radius)
        cy = h - radius - 1;

    dx = px - cx;
    dy = py - cy;

    return dx * dx + dy * dy <= radius * radius;
}

/**
 * @brief 绘制实心圆角矩形。
 *
 * @param[in] x 左上角 x 坐标。
 * @param[in] y 左上角 y 坐标。
 * @param[in] w 宽度。
 * @param[in] h 高度。
 * @param[in] radius 圆角半径。
 * @param[in] color 填充颜色。
 */
static void ui_fill_round_rect(int x,
                               int y,
                               int w,
                               int h,
                               int radius,
                               unsigned int color)
{
    int row;
    int col;

    for(row = 0; row < h; row++) {
        for(col = 0; col < w; col++) {
            if(ui_round_contains(col, row, w, h, radius))
                lcd_show(x + col, y + row, color);
        }
    }
}

/**
 * @brief 绘制带边框的圆角矩形。
 *
 * @param[in] x 左上角 x 坐标。
 * @param[in] y 左上角 y 坐标。
 * @param[in] w 宽度。
 * @param[in] h 高度。
 * @param[in] radius 圆角半径。
 * @param[in] thickness 边框厚度。
 * @param[in] border 边框颜色。
 * @param[in] fill 内部填充颜色。
 */
static void ui_draw_round_border(int x,
                                 int y,
                                 int w,
                                 int h,
                                 int radius,
                                 int thickness,
                                 unsigned int border,
                                 unsigned int fill)
{
    ui_fill_round_rect(x, y, w, h, radius, border);
    ui_fill_round_rect(x + thickness,
                       y + thickness,
                       w - thickness * 2,
                       h - thickness * 2,
                       radius - thickness,
                       fill);
}

/**
 * @brief 绘制统一风格的浅色面板。
 *
 * @param[in] x 左上角 x 坐标。
 * @param[in] y 左上角 y 坐标。
 * @param[in] w 宽度。
 * @param[in] h 高度。
 * @param[in] radius 圆角半径。
 * @param[in] fill 面板基础颜色。
 * @param[in] accent 强调色，用于底部细线。
 *
 * @details 面板会叠加浅色高光和强调色底线，作为首页卡片、监测卡片、
 * 历史报警列表等 UI 元素的基础样式。
 */
static void ui_draw_glass_panel(int x,
                                int y,
                                int w,
                                int h,
                                int radius,
                                unsigned int fill,
                                unsigned int accent)
{
    unsigned int soft_fill = ui_rgb_mix(fill, 0x00dcecff, 12);

    ui_draw_round_border(x, y, w, h, radius, 2, 0x00ffffff, soft_fill);
    ui_fill_round_rect(x + 12, y + 10, w - 24, 10, 5, 0x00ffffff);
    ui_fill_round_rect(x + 18, y + h - 13, w - 36, 4, 2,
                       ui_rgb_mix(accent, 0x00ffffff, 28));
}

/**
 * @brief 绘制水平线。
 */
static void ui_draw_hline(int x, int y, int w, unsigned int color)
{
    int col;

    for(col = 0; col < w; col++)
        lcd_show(x + col, y, color);
}

/**
 * @brief 绘制垂直线。
 */
static void ui_draw_vline(int x, int y, int h, unsigned int color)
{
    int row;

    for(row = 0; row < h; row++)
        lcd_show(x, y + row, color);
}

/**
 * @brief 绘制背景网格线。
 *
 * @param[in] color 网格线颜色。
 */
static void ui_draw_grid(unsigned int color)
{
    int x;
    int y;

    for(x = 0; x < LCD_W; x += 40)
        ui_draw_vline(x, 0, LCD_H, color);

    for(y = 0; y < LCD_H; y += 40)
        ui_draw_hline(0, y, LCD_W, color);
}

/**
 * @brief 绘制浅色渐变背景。
 *
 * @param[in] base 页面基础色。
 *
 * @details 使用逐行颜色混合做轻量渐变，不依赖图片资源；上下边缘再叠加
 * 亮色条，降低 LCD 边框附近的视觉压迫感。
 */
static void ui_draw_light_background(unsigned int base)
{
    int y;

    for(y = 0; y < LCD_H; y++) {
        unsigned int row_color = ui_rgb_mix(base, 0x00ffffff, y * 28 / LCD_H);
        ui_draw_hline(0, y, LCD_W, row_color);
    }

    ui_fill_rect(0, 0, LCD_W, 18, 0x00ffffff);
    ui_fill_rect(0, LCD_H - 18, LCD_W, 18, ui_rgb_mix(base, 0x00ffffff, 42));
}

/**
 * @brief 判断点是否位于矩形内。
 */
static int ui_point_in_rect(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

/**
 * @brief 根据页面 id 查找静态页面定义。
 *
 * @param[in] id 页面 id。
 *
 * @retval 非NULL 页面定义。
 * @retval NULL 未找到页面或 id 为空。
 */
static const ui_screen_def_t *ui_find_screen(const char *id)
{
    int i;

    if(id == NULL)
        return NULL;

    for(i = 0; i < ui_app_screen_count; i++) {
        if(strcmp(ui_app_screens[i].id, id) == 0)
            return &ui_app_screens[i];
    }

    return NULL;
}

/**
 * @brief 绘制一个静态页面按钮。
 *
 * @param[in] button 按钮定义。
 *
 * @details 首页大按钮带副标题和右侧箭头；顶部返回按钮没有副标题，
 * 会根据文字宽高居中绘制。
 */
static void ui_draw_button(const ui_button_def_t *button)
{
    int text_w = 0;
    int text_h = 0;
    int text_x;
    int text_y;
    int has_subtitle;

    if(button == NULL)
        return;

    has_subtitle = button->subtitle != NULL && button->subtitle[0] != '\0';

    ui_draw_glass_panel(button->x, button->y, button->w, button->h,
                        button->radius, button->bg, button->accent);
    ui_fill_round_rect(button->x + 18, button->y + 17, 12,
                       button->h - 34, 6, button->accent);
    if(button->w > 220) {
        ui_fill_round_rect(button->x + button->w - 70, button->y + 24,
                           44, 44, 22,
                           ui_rgb_mix(button->accent, 0x00ffffff, 18));
        lcd_word_show_color(button->x + button->w - 58, button->y + 29,
                            ">", 0x00ffffff);
    }

    if(has_subtitle) {
        lcd_word_show_color(button->x + 46, button->y + 9,
                            button->text, button->fg);
        lcd_word_show_color(button->x + 46, button->y + 48,
                            button->subtitle, 0x004f6276);
        return;
    }

    if(lcd_word_measure(button->text, &text_w, &text_h) != 0) {
        lcd_word_show_color(button->x + 18, button->y + 10,
                            button->text, button->fg);
        return;
    }

    text_x = button->x + (button->w - text_w) / 2 + 2;
    text_y = button->y + (button->h - text_h) / 2;
    lcd_word_show_color(text_x, text_y, button->text, button->fg);
}

/**
 * @brief 用按钮样式绘制一个临时矩形按钮。
 */
static void ui_draw_button_rect(int x,
                                int y,
                                int w,
                                int h,
                                const char *text,
                                unsigned int bg,
                                unsigned int fg)
{
    ui_button_def_t button = {"", text, "", x, y, w, h, bg, fg, fg, 24, ""};

    ui_draw_button(&button);
}

/**
 * @brief 绘制开关控件。
 *
 * @param[in] x 左上角 x 坐标。
 * @param[in] y 左上角 y 坐标。
 * @param[in] on 非 0 表示开启。
 * @param[in] accent 开启状态强调色。
 */
static void ui_draw_toggle(int x, int y, int on, unsigned int accent)
{
    unsigned int track = on ? ui_rgb_mix(accent, 0x00ffffff, 12) : 0x00dbe6f0;
    int knob_x = on ? x + DEVICE_TOGGLE_W - 38 : x + 4;

    ui_fill_round_rect(x, y, DEVICE_TOGGLE_W, DEVICE_TOGGLE_H,
                       DEVICE_TOGGLE_H / 2, track);
    ui_fill_round_rect(knob_x, y + 4, 32, 32, 16, 0x00ffffff);
}

/**
 * @brief 绘制 ON/OFF/N/A 状态文字。
 *
 * @param[in] x 文字 x 坐标。
 * @param[in] y 文字 y 坐标。
 * @param[in] on 当前状态。
 * @param[in] available 当前设备或传感器是否可用。
 */
static void ui_draw_status_text(int x, int y, int on, int available)
{
    if(!available) {
        lcd_word_show_color(x, y, "N/A", 0x00728496);
        return;
    }

    lcd_word_show_color(x, y, on ? "ON" : "OFF",
                        on ? 0x0000a75a : 0x005f7084);
}

/**
 * @brief 将 0.01 单位的有符号定点数格式化为两位小数。
 */
static void ui_format_signed_2(char *out, size_t out_size, int value)
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

/**
 * @brief 将 0.01 单位的无符号定点数格式化为两位小数。
 */
static void ui_format_unsigned_2(char *out, size_t out_size, unsigned int value)
{
    if(out == NULL || out_size == 0)
        return;

    snprintf(out, out_size, "%u.%02u", value / 100, value % 100);
}

/**
 * @brief 获取一个 UTF-8 字符占用的字节数。
 *
 * @param[in] lead UTF-8 字符首字节。
 *
 * @return 字符字节数。非法或不完整编码按 1 字节处理。
 */
static size_t ui_utf8_char_bytes(unsigned char lead)
{
    if((lead & 0x80U) == 0)
        return 1;

    if((lead & 0xe0U) == 0xc0U)
        return 2;

    if((lead & 0xf0U) == 0xe0U)
        return 3;

    if((lead & 0xf8U) == 0xf0U)
        return 4;

    return 1;
}

/**
 * @brief 按像素宽度裁剪 UTF-8 文本。
 *
 * @param[out] out 输出缓冲区。
 * @param[in] out_size 输出缓冲区大小。
 * @param[in] text 原始文本。
 * @param[in] max_w 最大显示宽度，单位像素。
 *
 * @details 裁剪时只在 UTF-8 字符边界截断，并在被截断时追加 ".."。
 */
static void ui_copy_text_fit(char *out,
                             size_t out_size,
                             const char *text,
                             int max_w)
{
    size_t used = 0;
    const char *cursor = text;
    int measured = 0;

    if(out == NULL || out_size == 0)
        return;

    out[0] = '\0';
    if(text == NULL)
        return;

    while(*cursor != '\0') {
        size_t char_len = ui_utf8_char_bytes((unsigned char)*cursor);

        if(strlen(cursor) < char_len)
            char_len = 1;

        if(used + char_len >= out_size)
            break;

        memcpy(out + used, cursor, char_len);
        used += char_len;
        out[used] = '\0';

        if(lcd_word_measure(out, &measured, NULL) == 0 && measured > max_w) {
            if(used >= char_len)
                used -= char_len;
            out[used] = '\0';
            break;
        }

        cursor += char_len;
    }

    if(*cursor != '\0' && used + 2 < out_size) {
        out[used++] = '.';
        out[used++] = '.';
        out[used] = '\0';
    }
}

/**
 * @brief 根据报警类型生成事件名称。
 *
 * @param[in] temperature_alarm 是否温度报警。
 * @param[in] smoke_alarm 是否烟雾报警。
 * @param[out] out 输出缓冲区。
 * @param[in] out_size 输出缓冲区大小。
 */
static void ui_alarm_event_name(int temperature_alarm,
                                int smoke_alarm,
                                char *out,
                                size_t out_size)
{
    if(out == NULL || out_size == 0)
        return;

    if(temperature_alarm && smoke_alarm)
        snprintf(out, out_size, "温度/烟雾报警");
    else if(temperature_alarm)
        snprintf(out, out_size, "温度报警");
    else if(smoke_alarm)
        snprintf(out, out_size, "烟雾报警");
    else
        snprintf(out, out_size, "报警");
}

/**
 * @brief 统计历史报警日志中的有效行数。
 *
 * @return 报警记录行数，最大限制为 999999。
 *
 * @details 会依次检查主日志、/root 后备日志和 /tmp 后备日志。这样程序
 * 从不同目录运行或工作目录不可写时，历史次数仍尽量可恢复。
 */
static unsigned int ui_alarm_count_log_entries(void)
{
    char line[ALARM_LOG_LINE_MAX];
    unsigned int count = 0;
    int i;

    for(i = 0; ui_alarm_log_candidate(i) != NULL; i++) {
        const char *path = ui_alarm_log_candidate(i);
        FILE *fp;

        if(i > 0 && strcmp(path, ui_alarm_log_path()) == 0)
            continue;

        fp = fopen(path, "r");
        if(fp == NULL)
            continue;

        while(fgets(line, sizeof(line), fp) != NULL) {
            if(line[0] != '\0' && line[0] != '\n' && count < 999999U)
                count++;
        }

        fclose(fp);
    }

    return count;
}

/**
 * @brief 生成一条报警日志记录。
 *
 * @param[in] temperature_alarm 是否温度报警。
 * @param[in] smoke_alarm 是否烟雾报警。
 * @param[out] item 输出日志条目。
 *
 * @details 记录格式在写入文件时为“时间|事件|详情”。详情里保存报警时
 * 的温度、烟雾原始值和阈值，便于历史页面直接展示。
 */
static void ui_alarm_make_log_item(int temperature_alarm,
                                   int smoke_alarm,
                                   ui_alarm_log_item_t *item)
{
    time_t now;
    struct tm tm_now;
    char temperature[24] = "--";
    char temp_threshold[24];

    if(item == NULL)
        return;

    now = time(NULL);
    if(localtime_r(&now, &tm_now) == NULL) {
        snprintf(item->time_text, sizeof(item->time_text),
                 "0000-00-00 00:00:00");
    } else {
        strftime(item->time_text, sizeof(item->time_text),
                 "%Y-%m-%d %H:%M:%S",
                 &tm_now);
    }

    if(sensor_state.gy39_ok && sensor_state.gy39.has_environment)
        ui_format_signed_2(temperature, sizeof(temperature),
                           sensor_state.gy39.temperature_centideg);
    ui_format_signed_2(temp_threshold, sizeof(temp_threshold),
                       sensor_state.temperature_threshold_centideg);
    ui_alarm_event_name(temperature_alarm, smoke_alarm,
                        item->event_text, sizeof(item->event_text));

    snprintf(item->detail_text, sizeof(item->detail_text),
             "温度 %s/%sC  烟雾 %u/%u",
             temperature,
             temp_threshold,
             sensor_state.smoke_ok && sensor_state.smoke.has_concentration ?
                 sensor_state.smoke.concentration : 0,
             sensor_state.smoke_threshold);
}

/**
 * @brief 将无法写入文件的报警记录暂存在内存中。
 *
 * @param[in] item 报警记录。
 *
 * @details 仅保留最近 ALARM_HISTORY_PAGE_MAX 条，用于文件系统不可写时
 * 仍能在本次运行中看到刚发生的报警。
 */
static void ui_alarm_remember_fallback(const ui_alarm_log_item_t *item)
{
    if(item == NULL)
        return;

    if(alarm_fallback_count < ALARM_HISTORY_PAGE_MAX) {
        alarm_fallback_items[alarm_fallback_count] = *item;
        alarm_fallback_count++;
        return;
    }

    memmove(&alarm_fallback_items[0],
            &alarm_fallback_items[1],
            (ALARM_HISTORY_PAGE_MAX - 1) * sizeof(alarm_fallback_items[0]));
    alarm_fallback_items[ALARM_HISTORY_PAGE_MAX - 1] = *item;
}

/**
 * @brief 追加一条报警记录到日志文件。
 *
 * @param[in] item 报警记录。
 *
 * @retval 0 写入成功。
 * @retval -1 item 为空或所有候选路径均写入失败。
 */
static int ui_alarm_append_log(const ui_alarm_log_item_t *item)
{
    int i;

    if(item == NULL)
        return -1;

    for(i = 0; ui_alarm_log_candidate(i) != NULL; i++) {
        const char *path = ui_alarm_log_candidate(i);
        FILE *fp;

        if(i > 0 && strcmp(path, ui_alarm_log_path()) == 0)
            continue;

        fp = fopen(path, "a");
        if(fp == NULL) {
            LOG_PERROR(path);
            continue;
        }

        if(fprintf(fp, "%s|%s|%s\n",
                   item->time_text,
                   item->event_text,
                   item->detail_text) < 0) {
            LOG_PERROR(path);
            fclose(fp);
            continue;
        }

        fclose(fp);
        return 0;
    }

    return -1;
}

/**
 * @brief 解析报警日志中的一行文本。
 *
 * @param[in] line 日志文件原始行。
 * @param[out] item 输出解析后的条目。
 *
 * @details 新格式为“时间|事件|详情”。如果读取到旧格式或损坏行，
 * 会尽量把整行作为事件文本显示，而不是丢弃记录。
 */
static void ui_alarm_parse_log_line(const char *line, ui_alarm_log_item_t *item)
{
    char buffer[ALARM_LOG_LINE_MAX];
    char *event;
    char *detail;

    if(line == NULL || item == NULL)
        return;

    snprintf(buffer, sizeof(buffer), "%s", line);
    buffer[strcspn(buffer, "\r\n")] = '\0';

    event = strchr(buffer, '|');
    if(event == NULL) {
        snprintf(item->time_text, sizeof(item->time_text), "--");
        snprintf(item->event_text, sizeof(item->event_text), "%s", buffer);
        item->detail_text[0] = '\0';
        return;
    }

    *event = '\0';
    event++;
    detail = strchr(event, '|');
    if(detail != NULL) {
        *detail = '\0';
        detail++;
    }

    snprintf(item->time_text, sizeof(item->time_text), "%s", buffer);
    snprintf(item->event_text, sizeof(item->event_text), "%s", event);
    snprintf(item->detail_text, sizeof(item->detail_text), "%s",
             detail != NULL ? detail : "");
}

/**
 * @brief 从持久化日志中加载最近报警记录。
 *
 * @param[out] items 输出数组。
 * @param[in] max_items 最多加载条数。
 *
 * @return 实际加载条数。
 */
static int ui_alarm_load_recent(ui_alarm_log_item_t *items, int max_items)
{
    char line[ALARM_LOG_LINE_MAX];
    int count = 0;
    int i;

    if(items == NULL || max_items <= 0)
        return 0;

    for(i = 0; ui_alarm_log_candidate(i) != NULL; i++) {
        const char *path = ui_alarm_log_candidate(i);
        FILE *fp;

        if(i > 0 && strcmp(path, ui_alarm_log_path()) == 0)
            continue;

        fp = fopen(path, "r");
        if(fp == NULL)
            continue;

        while(fgets(line, sizeof(line), fp) != NULL) {
            if(line[0] == '\0' || line[0] == '\n')
                continue;

            if(count < max_items) {
                ui_alarm_parse_log_line(line, &items[count]);
                count++;
            } else {
                memmove(&items[0],
                        &items[1],
                        (size_t)(max_items - 1) * sizeof(items[0]));
                ui_alarm_parse_log_line(line, &items[max_items - 1]);
            }
        }

        fclose(fp);
    }

    return count;
}

/**
 * @brief 加载历史报警页面要显示的记录。
 *
 * @param[out] items 输出数组。
 * @param[in] max_items 最多加载条数。
 *
 * @return 实际加载条数。
 *
 * @details 先读取文件日志，再合并本次运行中的内存 fallback 记录。
 */
static int ui_alarm_load_history(ui_alarm_log_item_t *items, int max_items)
{
    int count = ui_alarm_load_recent(items, max_items);
    int i;

    if(items == NULL || max_items <= 0)
        return 0;

    for(i = 0; i < alarm_fallback_count; i++) {
        if(count < max_items) {
            items[count] = alarm_fallback_items[i];
            count++;
        } else {
            memmove(&items[0],
                    &items[1],
                    (size_t)(max_items - 1) * sizeof(items[0]));
            items[max_items - 1] = alarm_fallback_items[i];
        }
    }

    return count;
}

/**
 * @brief 绘制监测页的一张传感器数据卡片。
 *
 * @param[in] x 左上角 x 坐标。
 * @param[in] y 左上角 y 坐标。
 * @param[in] title 卡片标题。
 * @param[in] value 当前值字符串。
 * @param[in] unit 单位字符串。
 * @param[in] ok 当前数据是否有效。
 * @param[in] alarm 当前数据是否触发报警。
 * @param[in] accent 正常状态强调色。
 */
static void ui_draw_sensor_card(int x,
                                int y,
                                const char *title,
                                const char *value,
                                const char *unit,
                                int ok,
                                int alarm,
                                unsigned int accent)
{
    unsigned int card_accent = alarm ? 0x00d92d20 : accent;
    unsigned int value_color;
    const char *status;
    int value_w = 0;
    int unit_w = 0;
    int status_w = 0;
    int unit_x;
    int status_x;

    if(alarm)
        value_color = 0x00d92d20;
    else
        value_color = ok ? accent : 0x00728496;

    if(alarm)
        status = "WARN";
    else
        status = ok ? "LIVE" : "OFF";

    ui_draw_glass_panel(x, y, 330, 112, 24, 0x00ffffff, card_accent);

    lcd_word_measure(value, &value_w, NULL);
    lcd_word_measure(unit, &unit_w, NULL);
    lcd_word_measure(status, &status_w, NULL);
    unit_x = x + 30 + value_w + 14;
    if(unit_x + unit_w > x + 208)
        unit_x = x + 208 - unit_w;
    status_x = x + 300 - status_w;

    lcd_word_show_color(x + 30, y + 16, title, 0x00485767);
    lcd_word_show_color(status_x, y + 16, status,
                        alarm ? 0x00d92d20 :
                        (ok ? 0x0000a75a : 0x005f7084));
    lcd_word_show_color(x + 30, y + 62, value, value_color);
    lcd_word_show_color(unit_x, y + 64, unit, 0x00667586);
}

/**
 * @brief 绘制监测页底部的报警状态和历史次数入口。
 */
static void ui_draw_alarm_history_panel(void)
{
    char status[64];
    char history[64];
    unsigned int accent = sensor_state.alarm_active ? 0x00d92d20 : 0x0000a75a;

    if(sensor_state.alarm_active) {
        if(sensor_state.temperature_alarm && sensor_state.smoke_alarm)
            snprintf(status, sizeof(status), "状态: 温度/烟雾报警");
        else if(sensor_state.temperature_alarm)
            snprintf(status, sizeof(status), "状态: 温度报警");
        else
            snprintf(status, sizeof(status), "状态: 烟雾报警");
    } else {
        snprintf(status, sizeof(status), "状态: 正常");
    }

    snprintf(history, sizeof(history), "历史报警: %u次",
             sensor_state.alarm_count);

    ui_draw_glass_panel(ALARM_HISTORY_PANEL_X,
                        ALARM_HISTORY_PANEL_Y,
                        ALARM_HISTORY_PANEL_W,
                        ALARM_HISTORY_PANEL_H,
                        20,
                        0x00ffffff,
                        accent);
    lcd_word_show_color(86, 404, status, accent);
    lcd_word_show_color(448, 404, history, 0x00172033);
    lcd_word_show_color(662, 404, ">", 0x00667586);
}

/**
 * @brief 绘制历史报警页面。
 *
 * @details 该页面同时提供声音报警开关，并展示最近几条报警记录。
 */
static void ui_draw_alarm_history_page(void)
{
    ui_alarm_log_item_t items[ALARM_HISTORY_PAGE_MAX];
    int count = ui_alarm_load_history(items, ALARM_HISTORY_PAGE_MAX);
    int i;

    ui_draw_glass_panel(54, 130, 692, 64, 22, 0x00ffffff, 0x00ff7a45);
    lcd_word_show_color(82, 146, "声音报警", 0x00172033);
    ui_draw_status_text(252, 146, sensor_state.alarm_sound_enabled, 1);
    ui_draw_toggle(ALARM_SOUND_TOGGLE_X,
                   ALARM_SOUND_TOGGLE_Y,
                   sensor_state.alarm_sound_enabled,
                   0x00ff7a45);

    ui_draw_glass_panel(54, 212, 692, 234, 24, 0x00ffffff, 0x00d92d20);
    lcd_word_show_color(82, 230, "最近报警", 0x00485767);
    ui_draw_hline(82, 270, 628, 0x00dfe7ef);

    if(count == 0) {
        lcd_word_show_color(274, 330, "暂无报警记录", 0x00909cab);
        return;
    }

    for(i = 0; i < count; i++) {
        int src = count - 1 - i;
        int y = ALARM_HISTORY_ROW_Y + i * ALARM_HISTORY_ROW_H;
        char detail_line[96];

        ui_copy_text_fit(detail_line,
                         sizeof(detail_line),
                         items[src].detail_text[0] != '\0' ?
                             items[src].detail_text : items[src].event_text,
                         ALARM_HISTORY_DETAIL_MAX_W);

        lcd_word_show_color(82, y, items[src].time_text, 0x00172033);
        lcd_word_show_color(456, y, items[src].event_text,
                            strcmp(items[src].event_text, "温度/烟雾报警") == 0 ?
                                0x00d92d20 : 0x00172033);
        lcd_word_show_color(82, y + 34, detail_line, 0x00667586);
    }
}

/**
 * @brief 绘制数据监测页面动态区域。
 *
 * @details GY-39 数据使用 0.01 单位定点数保存，绘制前格式化为两位小数。
 * 传感器不可用时显示 "--" 和 OFF，避免把旧值误当成实时值。
 */
static void ui_draw_monitor_panel(void)
{
    char temperature[24] = "--";
    char humidity[24] = "--";
    char lux[24] = "--";
    char smoke[24] = "--";
    char temp_threshold[24];
    char temperature_title[40];
    char smoke_title[40];
    int env_ok = sensor_state.gy39_ok && sensor_state.gy39.has_environment;
    int lux_ok = sensor_state.gy39_ok && sensor_state.gy39.has_lux;
    int smoke_ok = sensor_state.smoke_ok && sensor_state.smoke.has_concentration;

    if(env_ok) {
        ui_format_signed_2(temperature, sizeof(temperature),
                           sensor_state.gy39.temperature_centideg);
        ui_format_unsigned_2(humidity, sizeof(humidity),
                             sensor_state.gy39.humidity_centipercent);
    }

    if(lux_ok)
        ui_format_unsigned_2(lux, sizeof(lux),
                             sensor_state.gy39.lux_centilux);

    if(smoke_ok)
        snprintf(smoke, sizeof(smoke), "%u",
                 sensor_state.smoke.concentration);

    ui_format_signed_2(temp_threshold, sizeof(temp_threshold),
                       sensor_state.temperature_threshold_centideg);
    snprintf(temperature_title, sizeof(temperature_title), "温度>%sC",
             temp_threshold);
    snprintf(smoke_title, sizeof(smoke_title), "烟雾>%u",
             sensor_state.smoke_threshold);

    ui_draw_sensor_card(54, 136, temperature_title, temperature, "C", env_ok,
                        sensor_state.temperature_alarm,
                        0x00c2410c);
    ui_draw_sensor_card(416, 136, "湿度", humidity, "%RH", env_ok,
                        0,
                        0x002874ff);
    ui_draw_sensor_card(54, 274, "光照", lux, "lux", lux_ok,
                        0,
                        0x00b7791f);
    ui_draw_sensor_card(416, 274, smoke_title, smoke, "raw", smoke_ok,
                        sensor_state.smoke_alarm,
                        0x00d92d20);
    ui_draw_alarm_history_panel();
}

/**
 * @brief 根据报警状态同步蜂鸣器。
 *
 * @param[in] alarm_active 非 0 表示当前存在报警。
 *
 * @details 手动蜂鸣器状态 device_beep_state 和报警声音开关共同决定最终
 * 蜂鸣器输出。报警解除时只释放报警接管，不改变手动开关状态。
 */
static void ui_sensor_sync_alarm_beep(int alarm_active)
{
    int desired_beep = device_beep_state ||
                       (alarm_active && sensor_state.alarm_sound_enabled);

    if(sensor_state.alarm_beep_active == desired_beep)
        return;

    if(beep_ctl(desired_beep ? 1 : 0) == 0) {
        sensor_state.alarm_beep_active = desired_beep;
        return;
    }

    LOG_ERROR("sensor alarm set beep failed");
}

/**
 * @brief 清除当前报警标志并关闭报警接管的蜂鸣器。
 */
static void ui_sensor_clear_alarm(void)
{
    sensor_state.temperature_alarm = 0;
    sensor_state.smoke_alarm = 0;
    sensor_state.alarm_active = 0;
    ui_sensor_sync_alarm_beep(0);
}

/**
 * @brief 根据最新传感器数据更新报警状态。
 *
 * @details 只有从“无报警”进入“有报警”的边沿才写一条历史记录并增加
 * alarm_count。报警持续期间不会每个刷新周期重复记数。
 */
static void ui_sensor_update_alarm(void)
{
    int env_ok = sensor_state.gy39_ok && sensor_state.gy39.has_environment;
    int smoke_ok = sensor_state.smoke_ok && sensor_state.smoke.has_concentration;
    int temperature_alarm = env_ok &&
                            sensor_state.gy39.temperature_centideg >
                                sensor_state.temperature_threshold_centideg;
    int smoke_alarm = smoke_ok &&
                      sensor_state.smoke.concentration >
                          sensor_state.smoke_threshold;
    int alarm_active = temperature_alarm || smoke_alarm;

    if(alarm_active && !sensor_state.alarm_active) {
        ui_alarm_log_item_t item;

        ui_alarm_make_log_item(temperature_alarm, smoke_alarm, &item);
        if(ui_alarm_append_log(&item) != 0) {
            ui_alarm_remember_fallback(&item);
            LOG_WARN("alarm happened but log file was not updated");
        }
        sensor_state.alarm_count++;
    }

    sensor_state.temperature_alarm = temperature_alarm;
    sensor_state.smoke_alarm = smoke_alarm;
    sensor_state.alarm_active = alarm_active;
    ui_sensor_sync_alarm_beep(alarm_active);
}

/**
 * @brief 初始化传感器和报警配置。
 *
 * @details 初始化顺序为：读取配置文件、恢复历史报警次数、打开两个 UART、
 * 请求第一次非阻塞刷新。UART 打开失败不让整个 UI 退出，页面会显示 OFF。
 */
static void ui_sensor_init(void)
{
    const char *gy39_dev = ui_env_or_default("GEC6818_GY39_UART_DEV",
                                            SENSOR_GY39_UART_DEV);
    const char *smoke_dev = ui_env_or_default("GEC6818_SMOKE_UART_DEV",
                                             SENSOR_SMOKE_UART_DEV);

    ui_settings_load();
    sensor_state.alarm_count = ui_alarm_count_log_entries();

    sensor_state.gy39_fd = uarts_open(gy39_dev, GY39_DEFAULT_BAUD);
    if(sensor_state.gy39_fd < 0)
        LOG_WARN("open GY-39 uart failed: %s", gy39_dev);

    sensor_state.smoke_fd = uarts_open(smoke_dev, ZMQ01_DEFAULT_BAUD);
    if(sensor_state.smoke_fd < 0)
        LOG_WARN("open Z-MQ-01 uart failed: %s", smoke_dev);

    ui_sensor_request_refresh();
}

/**
 * @brief 释放传感器资源。
 */
static void ui_sensor_cleanup(void)
{
    ui_sensor_clear_alarm();
    uarts_close(sensor_state.gy39_fd);
    uarts_close(sensor_state.smoke_fd);
    sensor_state.gy39_fd = -1;
    sensor_state.smoke_fd = -1;
}

/**
 * @brief 从接收缓冲区头部丢弃指定字节数。
 *
 * @param[in,out] buffer 接收缓冲区。
 * @param[in,out] length 当前有效长度。
 * @param[in] count 要丢弃的字节数。
 */
static void ui_sensor_discard_bytes(unsigned char *buffer,
                                    size_t *length,
                                    size_t count)
{
    if(buffer == NULL || length == NULL || count == 0)
        return;

    if(count >= *length) {
        *length = 0;
        return;
    }

    memmove(buffer, buffer + count, *length - count);
    *length -= count;
}

/**
 * @brief 读取 UART 当前已经到达的数据到指定缓冲区。
 *
 * @param[in] fd UART 文件描述符。
 * @param[out] buffer 接收缓冲区。
 * @param[in,out] length 当前有效长度，成功读取后增加。
 * @param[in] capacity 缓冲区总容量。
 *
 * @retval 0 读取完成或当前无更多数据。
 * @retval -1 UART 读取失败。
 *
 * @details 本函数不等待新数据，只搬运内核缓冲区里已经到达的数据。
 */
static int ui_sensor_drain_uart(int fd,
                                unsigned char *buffer,
                                size_t *length,
                                size_t capacity)
{
    while(*length < capacity) {
        int nread = uarts_read_available(fd,
                                         buffer + *length,
                                         capacity - *length);

        if(nread < 0)
            return -1;

        if(nread == 0)
            break;

        *length += (size_t)nread;
    }

    return 0;
}

/**
 * @brief 从 GY-39 接收缓冲区弹出一帧完整数据。
 *
 * @param[out] frame 输出帧。
 *
 * @retval 1 成功弹出一帧。
 * @retval 0 当前缓冲区还没有完整帧。
 *
 * @details GY-39 帧以 0x5A 0x5A 开头，随后是类型、长度和数据区。
 * 函数会丢弃帧头前的噪声字节，并保留跨轮询到达的半帧。
 */
static int ui_sensor_pop_gy39_frame(gy39_frame_t *frame)
{
    size_t i;

    if(frame == NULL)
        return 0;

    while(sensor_state.gy39_rx_len >= 2) {
        for(i = 0; i + 1 < sensor_state.gy39_rx_len; i++) {
            if(sensor_state.gy39_rx[i] == UI_GY39_FRAME_HEAD &&
               sensor_state.gy39_rx[i + 1] == UI_GY39_FRAME_HEAD)
                break;
        }

        if(i + 1 >= sensor_state.gy39_rx_len) {
            if(sensor_state.gy39_rx[sensor_state.gy39_rx_len - 1] ==
               UI_GY39_FRAME_HEAD) {
                sensor_state.gy39_rx[0] =
                    sensor_state.gy39_rx[sensor_state.gy39_rx_len - 1];
                sensor_state.gy39_rx_len = 1;
            } else {
                sensor_state.gy39_rx_len = 0;
            }
            return 0;
        }

        if(i > 0)
            ui_sensor_discard_bytes(sensor_state.gy39_rx,
                                    &sensor_state.gy39_rx_len,
                                    i);

        if(sensor_state.gy39_rx_len < 4)
            return 0;

        if(sensor_state.gy39_rx[3] > GY39_MAX_DATA_LEN) {
            ui_sensor_discard_bytes(sensor_state.gy39_rx,
                                    &sensor_state.gy39_rx_len,
                                    1);
            continue;
        }

        if(sensor_state.gy39_rx_len < 4U + sensor_state.gy39_rx[3])
            return 0;

        memset(frame, 0, sizeof(*frame));
        frame->type = sensor_state.gy39_rx[2];
        frame->length = sensor_state.gy39_rx[3];
        memcpy(frame->data, sensor_state.gy39_rx + 4, frame->length);
        ui_sensor_discard_bytes(sensor_state.gy39_rx,
                                &sensor_state.gy39_rx_len,
                                4U + frame->length);
        return 1;
    }

    return 0;
}

/**
 * @brief 从 Z-MQ-01 接收缓冲区弹出一帧 9 字节响应。
 *
 * @param[out] raw 输出原始 9 字节帧。
 *
 * @retval 1 成功弹出一帧。
 * @retval 0 当前缓冲区还没有完整帧。
 */
static int ui_sensor_pop_smoke_frame(unsigned char raw[ZMQ01_FRAME_SIZE])
{
    size_t i;

    if(raw == NULL)
        return 0;

    while(sensor_state.smoke_rx_len >= ZMQ01_FRAME_SIZE) {
        for(i = 0; i < sensor_state.smoke_rx_len; i++) {
            if(sensor_state.smoke_rx[i] == UI_ZMQ01_FRAME_HEAD)
                break;
        }

        if(i >= sensor_state.smoke_rx_len) {
            sensor_state.smoke_rx_len = 0;
            return 0;
        }

        if(i > 0)
            ui_sensor_discard_bytes(sensor_state.smoke_rx,
                                    &sensor_state.smoke_rx_len,
                                    i);

        if(sensor_state.smoke_rx_len < ZMQ01_FRAME_SIZE)
            return 0;

        memcpy(raw, sensor_state.smoke_rx, ZMQ01_FRAME_SIZE);
        ui_sensor_discard_bytes(sensor_state.smoke_rx,
                                &sensor_state.smoke_rx_len,
                                ZMQ01_FRAME_SIZE);
        return 1;
    }

    return 0;
}

/**
 * @brief 启动 GY-39 光照查询阶段。
 *
 * @param[in] now 当前毫秒时间。
 *
 * @retval 0 查询命令已发送。
 * @retval -1 GY-39 UART 不可用或发送失败。
 */
static int ui_sensor_begin_gy39_lux(long long now)
{
    int timeout_ms;

    if(sensor_state.gy39_fd < 0)
        return -1;

    uarts_flush_input(sensor_state.gy39_fd);
    sensor_state.gy39_rx_len = 0;
    sensor_state.gy39_cycle_ok = 0;
    if(gy39_send_command(sensor_state.gy39_fd, GY39_CMD_QUERY_LUX) != 0)
        return -1;

    timeout_ms = ui_env_int_or_default("GEC6818_GY39_LUX_TIMEOUT_MS",
                                       SENSOR_GY39_LUX_TIMEOUT_MS,
                                       SENSOR_GY39_MIN_TIMEOUT_MS,
                                       SENSOR_GY39_MAX_TIMEOUT_MS);
    sensor_state.poll_step = SENSOR_POLL_GY39_LUX;
    sensor_state.poll_deadline_ms = now + timeout_ms;
    return 0;
}

/**
 * @brief 启动 GY-39 环境数据查询阶段。
 *
 * @param[in] now 当前毫秒时间。
 *
 * @retval 0 查询命令已发送。
 * @retval -1 GY-39 UART 不可用或发送失败。
 */
static int ui_sensor_begin_gy39_env(long long now)
{
    int timeout_ms;

    if(sensor_state.gy39_fd < 0)
        return -1;

    uarts_flush_input(sensor_state.gy39_fd);
    sensor_state.gy39_rx_len = 0;
    if(gy39_send_command(sensor_state.gy39_fd, GY39_CMD_QUERY_ENV) != 0)
        return -1;

    timeout_ms = ui_env_int_or_default("GEC6818_GY39_ENV_TIMEOUT_MS",
                                       SENSOR_GY39_ENV_TIMEOUT_MS,
                                       SENSOR_GY39_MIN_TIMEOUT_MS,
                                       SENSOR_GY39_MAX_TIMEOUT_MS);
    sensor_state.poll_step = SENSOR_POLL_GY39_ENV;
    sensor_state.poll_deadline_ms = now + timeout_ms;
    return 0;
}

/**
 * @brief 启动 Z-MQ-01 烟雾浓度查询阶段。
 *
 * @param[in] now 当前毫秒时间。
 *
 * @retval 0 查询命令已发送。
 * @retval -1 烟雾传感器 UART 不可用或发送失败。
 */
static int ui_sensor_begin_smoke(long long now)
{
    if(sensor_state.smoke_fd < 0)
        return -1;

    uarts_flush_input(sensor_state.smoke_fd);
    sensor_state.smoke_rx_len = 0;
    if(zmq01_send_read_command(sensor_state.smoke_fd) != 0)
        return -1;

    sensor_state.poll_step = SENSOR_POLL_SMOKE;
    sensor_state.poll_deadline_ms = now + SENSOR_SMOKE_TIMEOUT_MS;
    return 0;
}

/**
 * @brief 更新 GY-39 可用状态。
 *
 * @param[in] ok 新状态。
 *
 * @retval 1 状态发生变化，需要重绘页面。
 * @retval 0 状态未变化。
 */
static int ui_sensor_set_gy39_ok(int ok)
{
    int old_ok = sensor_state.gy39_ok;

    sensor_state.gy39_ok = ok;
    return old_ok != sensor_state.gy39_ok;
}

/**
 * @brief 更新烟雾传感器可用状态。
 *
 * @param[in] ok 新状态。
 *
 * @retval 1 状态发生变化，需要重绘页面。
 * @retval 0 状态未变化。
 */
static int ui_sensor_set_smoke_ok(int ok)
{
    int old_ok = sensor_state.smoke_ok;

    sensor_state.smoke_ok = ok;
    return old_ok != sensor_state.smoke_ok;
}

/**
 * @brief 结束一轮传感器刷新。
 *
 * @param[in] now 当前毫秒时间。
 *
 * @retval 1 报警状态或历史次数发生变化，需要重绘。
 * @retval 0 页面可见状态未变化。
 */
static int ui_sensor_finish_cycle(long long now)
{
    int old_temperature_alarm = sensor_state.temperature_alarm;
    int old_smoke_alarm = sensor_state.smoke_alarm;
    int old_alarm_active = sensor_state.alarm_active;
    unsigned int old_alarm_count = sensor_state.alarm_count;

    ui_sensor_update_alarm();
    sensor_state.poll_step = SENSOR_POLL_IDLE;
    sensor_state.next_refresh_ms = now + SENSOR_REFRESH_INTERVAL_MS;

    return old_temperature_alarm != sensor_state.temperature_alarm ||
           old_smoke_alarm != sensor_state.smoke_alarm ||
           old_alarm_active != sensor_state.alarm_active ||
           old_alarm_count != sensor_state.alarm_count;
}

/**
 * @brief 从空闲状态启动下一轮可用传感器刷新。
 *
 * @param[in] now 当前毫秒时间。
 *
 * @retval 1 可用状态变化，需要重绘。
 * @retval 0 成功启动查询或状态无变化。
 *
 * @details 优先刷新 GY-39，再刷新烟雾传感器。若某个传感器不可用，
 * 状态机会跳过该阶段并更新对应 OFF 状态。
 */
static int ui_sensor_start_next_available(long long now)
{
    int redraw = 0;

    if(ui_sensor_begin_gy39_lux(now) == 0)
        return 0;

    redraw |= ui_sensor_set_gy39_ok(0);
    if(ui_sensor_begin_smoke(now) == 0)
        return redraw;

    redraw |= ui_sensor_set_smoke_ok(0);
    redraw |= ui_sensor_finish_cycle(now);
    return redraw;
}

/**
 * @brief 推进一次传感器非阻塞状态机。
 *
 * @retval 1 页面显示内容发生变化，需要重绘当前页面。
 * @retval 0 本次没有可见变化。
 *
 * @details 本函数不能调用阻塞式整帧读取。它只读取已经到达的 UART 数据，
 * 然后根据当前阶段决定是否解析帧、进入下一阶段或结束本轮刷新。
 */
static int ui_sensor_poll(void)
{
    long long now = ui_now_ms();
    int redraw = 0;

    if(sensor_state.poll_step == SENSOR_POLL_IDLE) {
        if(now < sensor_state.next_refresh_ms)
            return 0;
        return ui_sensor_start_next_available(now);
    }

    if(sensor_state.poll_step == SENSOR_POLL_GY39_LUX) {
        gy39_frame_t frame;

        if(ui_sensor_drain_uart(sensor_state.gy39_fd,
                                sensor_state.gy39_rx,
                                &sensor_state.gy39_rx_len,
                                sizeof(sensor_state.gy39_rx)) != 0) {
            redraw |= ui_sensor_set_gy39_ok(0);
            if(ui_sensor_begin_smoke(now) != 0) {
                redraw |= ui_sensor_set_smoke_ok(0);
                redraw |= ui_sensor_finish_cycle(now);
            }
            return 1 | redraw;
        }

        while(ui_sensor_pop_gy39_frame(&frame)) {
            if(frame.type == GY39_FRAME_LUX &&
               gy39_parse_frame(&frame, &sensor_state.gy39) == 0) {
                sensor_state.gy39_cycle_ok = 1;
                redraw = 1;
                break;
            }
        }

        if(redraw || now >= sensor_state.poll_deadline_ms) {
            if(ui_sensor_begin_gy39_env(now) != 0) {
                redraw |= ui_sensor_set_gy39_ok(sensor_state.gy39_cycle_ok);
                if(ui_sensor_begin_smoke(now) != 0) {
                    redraw |= ui_sensor_set_smoke_ok(0);
                    redraw |= ui_sensor_finish_cycle(now);
                }
            }
        }

        return redraw;
    }

    if(sensor_state.poll_step == SENSOR_POLL_GY39_ENV) {
        gy39_frame_t frame;

        if(ui_sensor_drain_uart(sensor_state.gy39_fd,
                                sensor_state.gy39_rx,
                                &sensor_state.gy39_rx_len,
                                sizeof(sensor_state.gy39_rx)) != 0) {
            redraw |= ui_sensor_set_gy39_ok(0);
            if(ui_sensor_begin_smoke(now) != 0) {
                redraw |= ui_sensor_set_smoke_ok(0);
                redraw |= ui_sensor_finish_cycle(now);
            }
            return 1 | redraw;
        }

        while(ui_sensor_pop_gy39_frame(&frame)) {
            if(frame.type == GY39_FRAME_ENV &&
               gy39_parse_frame(&frame, &sensor_state.gy39) == 0) {
                sensor_state.gy39_cycle_ok = 1;
                redraw = 1;
                break;
            }
        }

        if(redraw || now >= sensor_state.poll_deadline_ms) {
            redraw |= ui_sensor_set_gy39_ok(sensor_state.gy39_cycle_ok);
            if(ui_sensor_begin_smoke(now) != 0) {
                redraw |= ui_sensor_set_smoke_ok(0);
                redraw |= ui_sensor_finish_cycle(now);
            }
        }

        return redraw;
    }

    if(sensor_state.poll_step == SENSOR_POLL_SMOKE) {
        unsigned char raw[ZMQ01_FRAME_SIZE];
        int got_smoke = 0;

        if(ui_sensor_drain_uart(sensor_state.smoke_fd,
                                sensor_state.smoke_rx,
                                &sensor_state.smoke_rx_len,
                                sizeof(sensor_state.smoke_rx)) != 0) {
            redraw |= ui_sensor_set_smoke_ok(0);
            redraw |= ui_sensor_finish_cycle(now);
            return 1 | redraw;
        }

        while(ui_sensor_pop_smoke_frame(raw)) {
            if(zmq01_parse_response(raw, &sensor_state.smoke) == 0) {
                got_smoke = 1;
                redraw = 1;
                break;
            }
        }

        if(got_smoke || now >= sensor_state.poll_deadline_ms) {
            redraw |= ui_sensor_set_smoke_ok(got_smoke);
            redraw |= ui_sensor_finish_cycle(now);
        }

        return redraw;
    }

    sensor_state.poll_step = SENSOR_POLL_IDLE;
    return 0;
}

/**
 * @brief 请求尽快开始一次传感器刷新。
 *
 * @details 切入监测页或初始化完成后调用。函数只重置状态机时间点，
 * 不直接读取传感器，因此不会造成页面切换卡顿。
 */
static void ui_sensor_request_refresh(void)
{
    sensor_state.poll_step = SENSOR_POLL_IDLE;
    sensor_state.next_refresh_ms = 0;
    sensor_state.poll_deadline_ms = 0;
    sensor_state.gy39_rx_len = 0;
    sensor_state.smoke_rx_len = 0;
}

/**
 * @brief 判断页面是否需要传感器状态机持续刷新。
 *
 * @param[in] screen 页面定义。
 *
 * @retval 1 页面依赖传感器或报警状态。
 * @retval 0 页面不依赖传感器状态。
 */
static int ui_screen_uses_sensor(const ui_screen_def_t *screen)
{
    if(screen == NULL)
        return 0;

    return strcmp(screen->id, "monitor") == 0 ||
           strcmp(screen->id, "alarm_history") == 0;
}

/**
 * @brief 绘制设备控制页面的动态区域。
 *
 * @details 页面静态标题和返回按钮由 ui_draw_screen() 绘制；本函数只绘制
 * LED 列表、蜂鸣器状态和对应开关。
 */
static void ui_draw_device_mock(void)
{
    static const char *led_labels[DEVICE_LED_COUNT] = {
        "D7", "D8", "D9", "D10"
    };
    int i;

    ui_draw_glass_panel(DEVICE_LED_CARD_X, DEVICE_LED_CARD_Y,
                        DEVICE_LED_CARD_W, DEVICE_LED_CARD_H,
                        30, 0x00ffffff, 0x002874ff);
    lcd_word_show_color(92, 158, "LED 灯", 0x00172033);

    for(i = 0; i < DEVICE_LED_COUNT; i++) {
        int y = 196 + i * 42;
        int available = device_led_ready && i < device_led_count;

        lcd_word_show_color(92, y + 6, led_labels[i], 0x00485767);
        ui_draw_status_text(188, y + 6, device_led_state[i], available);
        ui_draw_toggle(366, y, available && device_led_state[i], 0x002874ff);
    }

    ui_draw_glass_panel(DEVICE_BEEP_CARD_X, DEVICE_BEEP_CARD_Y,
                        DEVICE_BEEP_CARD_W, DEVICE_BEEP_CARD_H,
                        30, 0x00ffffff, 0x00ff7a45);
    lcd_word_show_color(570, 168, "蜂鸣器", 0x00172033);
    ui_draw_status_text(604, 226, device_beep_state, 1);
    ui_draw_toggle(590, 286, device_beep_state, 0x00ff7a45);
}

/**
 * @brief 绘制完整页面。
 *
 * @param[in] screen 页面定义。
 *
 * @details 绘制顺序为背景、标题区域、静态标签、页面动态内容、按钮。
 * 所有内容先进入离屏缓冲，最后 ui_frame_end() 一次性刷新到 LCD。
 */
static void ui_draw_screen(const ui_screen_def_t *screen)
{
    int i;

    if(screen == NULL)
        return;

    ui_frame_begin();
    ui_draw_light_background(screen->background);
    ui_draw_grid(0x00e4edf5);
    ui_draw_glass_panel(34, 20, 732, 96, 32, 0x00ffffff, 0x002874ff);

    for(i = 0; i < screen->label_count; i++) {
        lcd_word_show_color(screen->labels[i].x,
                            screen->labels[i].y,
                            screen->labels[i].text,
                            screen->labels[i].color);
    }

    if(strcmp(screen->id, "monitor") == 0)
        ui_draw_monitor_panel();
    else if(strcmp(screen->id, "alarm_history") == 0)
        ui_draw_alarm_history_page();
    else if(strcmp(screen->id, "device") == 0)
        ui_draw_device_mock();

    for(i = 0; i < screen->button_count; i++)
        ui_draw_button(&screen->buttons[i]);

    ui_frame_end();
}

/**
 * @brief 绘制相册页面覆盖层。
 *
 * @param[in] index 当前图片下标。
 * @param[in] count 图片总数。
 *
 * @details 覆盖层包括返回按钮和页码。页码面板根据文字宽度动态计算，
 * 避免总页数位数变多后伸出面板。
 */
static void ui_draw_album_overlay(int index, int count)
{
    char status[64];
    int text_w = 0;
    int panel_w;
    int panel_x;
    int text_x;

    ui_draw_button_rect(ALBUM_BACK_X, ALBUM_BACK_Y,
                        ALBUM_BACK_W, ALBUM_BACK_H,
                        "返回", 0x00ffffff, 0x00172033);

    snprintf(status, sizeof(status), "%d / %d", index + 1, count);
    if(lcd_word_measure(status, &text_w, NULL) != 0)
        text_w = 80;

    panel_w = text_w + ALBUM_PAGE_PANEL_PAD_X * 2;
    if(panel_w < ALBUM_PAGE_PANEL_MIN_W)
        panel_w = ALBUM_PAGE_PANEL_MIN_W;
    if(panel_w > LCD_W - ALBUM_PAGE_PANEL_PAD_X * 2)
        panel_w = LCD_W - ALBUM_PAGE_PANEL_PAD_X * 2;

    panel_x = (LCD_W - panel_w) / 2;
    text_x = panel_x + (panel_w - text_w) / 2;
    if(text_x < panel_x + ALBUM_PAGE_PANEL_PAD_X)
        text_x = panel_x + ALBUM_PAGE_PANEL_PAD_X;

    ui_draw_glass_panel(panel_x,
                        ALBUM_PAGE_PANEL_Y,
                        panel_w,
                        ALBUM_PAGE_PANEL_H,
                        24,
                        0x00ffffff,
                        0x002874ff);
    lcd_word_show_color(text_x, 28, status, 0x00172033);
}

/**
 * @brief 加载并显示当前相册图片。
 *
 * @param[in] images 图片列表。
 * @param[in,out] frame 相册离屏帧缓存。
 * @param[in] index 要显示的图片下标。
 * @param[in,out] current_pic 当前图片缓存。
 *
 * @retval 0 加载并显示成功。
 * @retval -1 图片加载失败。
 * @retval -2 刷新图片缓存失败。
 */
static int ui_album_load_current(const image_list_t *images,
                                 frame_cache_t *frame,
                                 int index,
                                 pic_cache_t *current_pic)
{
    if(load_cache_at(images, index, current_pic) != 0)
        return -1;

    if(show_cached_image(frame, current_pic) != 0)
        return -2;

    ui_draw_album_overlay(index, images->count);
    return 0;
}

/**
 * @brief 运行电子相册子流程。
 *
 * @retval 0 正常返回上一页面。
 * @retval <0 图片列表、缓存或触摸读取失败。
 *
 * @details 相册是一个临时子循环，进入后接管触摸事件直到点击返回。
 * 为了滑动流畅，始终缓存当前图、上一张和下一张三张页面；滑动完成后
 * 通过结构体赋值转移缓存所有权，再补加载新的邻居图片。
 */
static int ui_run_album(void)
{
    image_list_t images = {0};
    frame_cache_t frame = {NULL};
    pic_cache_t current_pic = {0, 0, NULL};
    pic_cache_t prev_pic = {0, 0, NULL};
    pic_cache_t next_pic = {0, 0, NULL};
    int current_index = 0;
    int down_x = 0;
    int down_y = 0;
    int has_down = 0;
    int back_down = 0;
    int ret = 0;

    if(load_image_list(&images, PIC_DIR) != 0)
        return -1;

    if(images.count == 0) {
        LOG_ERROR("no image found in %s", PIC_DIR);
        image_list_free(&images);
        return -2;
    }

    if(frame_cache_init(&frame) != 0) {
        image_list_free(&images);
        return -3;
    }

    if(ui_album_load_current(&images, &frame, current_index, &current_pic) != 0) {
        ret = -4;
        goto cleanup;
    }

    if(load_neighbor_caches(&images, current_index, &prev_pic, &next_pic) != 0) {
        ret = -5;
        goto cleanup;
    }

    while(1) {
        touch_event_t event;

        if(touch_read(&event) != 0) {
            ret = -6;
            break;
        }

        if(event.type == TOUCH_EVENT_DOWN) {
            down_x = event.x;
            down_y = event.y;
            has_down = 1;
            back_down = ui_point_in_rect(event.x, event.y,
                                         ALBUM_BACK_X, ALBUM_BACK_Y,
                                         ALBUM_BACK_W, ALBUM_BACK_H);
        } else if(event.type == TOUCH_EVENT_UP && has_down) {
            int dx = event.x - down_x;
            int dy = event.y - down_y;
            int completed = abs(dx) >= ALBUM_SWIPE_MIN_DISTANCE &&
                            abs(dx) > abs(dy);
            int final_offset = 0;
            int target_is_next = 0;
            int target_index = current_index;
            pic_cache_t *target_pic = NULL;

            has_down = 0;

            if(back_down && ui_point_in_rect(event.x, event.y,
                                             ALBUM_BACK_X, ALBUM_BACK_Y,
                                             ALBUM_BACK_W, ALBUM_BACK_H)) {
                break;
            }

            back_down = 0;

            if(images.count <= 1 || abs(dx) <= abs(dy)) {
                show_cached_image(&frame, &current_pic);
                ui_draw_album_overlay(current_index, images.count);
                continue;
            }

            if(dx < 0) {
                target_is_next = 1;
                target_index = next_index(current_index, images.count);
                target_pic = &next_pic;
            } else if(dx > 0) {
                target_is_next = 0;
                target_index = prev_index(current_index, images.count);
                target_pic = &prev_pic;
            }

            if(target_pic == NULL || target_pic->pixels == NULL) {
                show_cached_image(&frame, &current_pic);
                ui_draw_album_overlay(current_index, images.count);
                continue;
            }

            final_offset = completed ? (target_is_next ? -LCD_W : LCD_W) : 0;
            animate_slide(&frame, &current_pic, target_pic,
                          clamp_slide_offset(dx), final_offset, target_is_next);

            if(completed) {
                if(target_is_next) {
                    pic_cache_free(&prev_pic);
                    prev_pic = current_pic;
                    current_pic = next_pic;
                    next_pic = (pic_cache_t){0, 0, NULL};
                    current_index = target_index;
                    show_cached_image(&frame, &current_pic);
                    ui_draw_album_overlay(current_index, images.count);

                    if(load_cache_at(&images,
                                     next_index(current_index, images.count),
                                     &next_pic) != 0) {
                        ret = -7;
                        break;
                    }
                } else {
                    pic_cache_free(&next_pic);
                    next_pic = current_pic;
                    current_pic = prev_pic;
                    prev_pic = (pic_cache_t){0, 0, NULL};
                    current_index = target_index;
                    show_cached_image(&frame, &current_pic);
                    ui_draw_album_overlay(current_index, images.count);

                    if(load_cache_at(&images,
                                     prev_index(current_index, images.count),
                                     &prev_pic) != 0) {
                        ret = -8;
                        break;
                    }
                }
            } else {
                show_cached_image(&frame, &current_pic);
                ui_draw_album_overlay(current_index, images.count);
            }
        } else if(event.type == TOUCH_EVENT_MOVE && has_down && !back_down) {
            int dx = event.x - down_x;
            int dy = event.y - down_y;
            int target_is_next;
            pic_cache_t *target_pic;

            if(images.count <= 1 || abs(dx) <= abs(dy))
                continue;

            if(dx < 0) {
                target_pic = &next_pic;
                target_is_next = 1;
            } else if(dx > 0) {
                target_pic = &prev_pic;
                target_is_next = 0;
            } else {
                continue;
            }

            if(target_pic->pixels == NULL)
                continue;

            draw_slide_frame(&frame, &current_pic, target_pic,
                             clamp_slide_offset(dx), target_is_next);
        }
    }

cleanup:
    pic_cache_free(&next_pic);
    pic_cache_free(&prev_pic);
    pic_cache_free(&current_pic);
    frame_cache_free(&frame);
    image_list_free(&images);

    return ret;
}

/**
 * @brief 初始化 LED 和蜂鸣器状态。
 *
 * @retval 0 初始化流程完成。
 *
 * @details LED 初始化失败不阻止程序运行，设备页会把不可用 LED 显示为
 * N/A。蜂鸣器默认关闭，由设备页或报警逻辑按需开启。
 */
static int ui_device_init(void)
{
    int i;

    device_led_count = led_init();
    if(device_led_count > DEVICE_LED_COUNT)
        device_led_count = DEVICE_LED_COUNT;

    device_led_ready = device_led_count > 0;
    for(i = 0; i < DEVICE_LED_COUNT; i++) {
        device_led_state[i] = 0;
        if(device_led_ready && i < device_led_count)
            led_set(i, 0);
    }

    device_beep_state = 0;
    beep_ctl(0);

    if(!device_led_ready)
        LOG_WARN("device panel led init failed");

    return 0;
}

/**
 * @brief 关闭设备输出并释放 LED/PWM 模块。
 *
 * @details 程序退出时统一熄灭 LED、关闭 sysfs PWM 和课程蜂鸣器设备，
 * 避免应用异常退出后外设仍保持响铃或点亮状态。
 */
static void ui_device_cleanup(void)
{
    int i;

    for(i = 0; i < device_led_count && i < DEVICE_LED_COUNT; i++)
        led_set(i, 0);

    led_uninit();
    pwm_beep_stop();
    beep_ctl(0);
}

/**
 * @brief 判断触摸点是否命中某个 LED 开关。
 *
 * @param[in] x 触摸 x 坐标。
 * @param[in] y 触摸 y 坐标。
 * @param[out] led_index 命中的 LED 下标，可为 NULL。
 *
 * @retval 1 命中 LED 开关。
 * @retval 0 未命中。
 */
static int ui_device_led_toggle_hit(int x, int y, int *led_index)
{
    int i;

    for(i = 0; i < DEVICE_LED_COUNT; i++) {
        int toggle_x = 366;
        int toggle_y = 196 + i * 42;

        if(ui_point_in_rect(x, y, toggle_x, toggle_y,
                            DEVICE_TOGGLE_W, DEVICE_TOGGLE_H)) {
            if(led_index != NULL)
                *led_index = i;
            return 1;
        }
    }

    return 0;
}

/**
 * @brief 处理设备页触摸事件。
 *
 * @param[in] event 触摸事件。
 *
 * @retval 1 事件已处理，当前页面需要重绘。
 * @retval 0 事件不属于设备页动态控件。
 */
static int ui_device_handle_touch(const touch_event_t *event)
{
    int led_index;

    if(event == NULL || event->type != TOUCH_EVENT_UP)
        return 0;

    if(ui_device_led_toggle_hit(event->x, event->y, &led_index)) {
        int next_state;

        if(!device_led_ready || led_index >= device_led_count)
            return 1;

        next_state = !device_led_state[led_index];
        if(led_set(led_index, next_state) == 0)
            device_led_state[led_index] = next_state;
        else
            LOG_ERROR("device panel set led %d failed", led_index);

        return 1;
    }

    if(ui_point_in_rect(event->x, event->y, 590, 286,
                        DEVICE_TOGGLE_W, DEVICE_TOGGLE_H)) {
        int next_state = !device_beep_state;

        device_beep_state = next_state;
        ui_sensor_sync_alarm_beep(sensor_state.alarm_active);

        return 1;
    }

    return 0;
}

/**
 * @brief 处理历史报警页触摸事件。
 *
 * @param[in] event 触摸事件。
 *
 * @retval 1 声音报警开关被切换。
 * @retval 0 未命中历史报警页动态控件。
 */
static int ui_alarm_history_handle_touch(const touch_event_t *event)
{
    if(event == NULL || event->type != TOUCH_EVENT_UP)
        return 0;

    if(!ui_point_in_rect(event->x,
                         event->y,
                         ALARM_SOUND_TOGGLE_X,
                         ALARM_SOUND_TOGGLE_Y,
                         DEVICE_TOGGLE_W,
                         DEVICE_TOGGLE_H))
        return 0;

    sensor_state.alarm_sound_enabled = !sensor_state.alarm_sound_enabled;
    ui_settings_save();
    ui_sensor_sync_alarm_beep(sensor_state.alarm_active);
    return 1;
}

/**
 * @brief 分发当前页面的触摸事件。
 *
 * @param[in] screen 当前页面。
 * @param[in] event 触摸事件。
 *
 * @retval 非NULL 触摸后应该显示的页面。
 *
 * @details 页面内动态控件优先处理；随后处理监测页历史报警入口；最后
 * 扫描静态按钮。相册按钮进入独立子流程，返回后重绘原页面。
 */
static const ui_screen_def_t *ui_handle_touch(const ui_screen_def_t *screen,
                                              const touch_event_t *event)
{
    int i;

    if(screen == NULL || event == NULL || event->type != TOUCH_EVENT_UP)
        return screen;

    if(strcmp(screen->id, "device") == 0 && ui_device_handle_touch(event)) {
        ui_draw_screen(screen);
        return screen;
    }

    if(strcmp(screen->id, "alarm_history") == 0 &&
       ui_alarm_history_handle_touch(event)) {
        ui_draw_screen(screen);
        return screen;
    }

    if(strcmp(screen->id, "monitor") == 0 &&
       ui_point_in_rect(event->x,
                        event->y,
                        ALARM_HISTORY_PANEL_X,
                        ALARM_HISTORY_PANEL_Y,
                        ALARM_HISTORY_PANEL_W,
                        ALARM_HISTORY_PANEL_H)) {
        const ui_screen_def_t *target = ui_find_screen("alarm_history");
        return target != NULL ? target : screen;
    }

    for(i = 0; i < screen->button_count; i++) {
        const ui_button_def_t *button = &screen->buttons[i];

        if(ui_point_in_rect(event->x, event->y,
                            button->x, button->y, button->w, button->h)) {
            if(strcmp(button->target, "album") == 0) {
                ui_run_album();
                ui_draw_screen(screen);
                return screen;
            }

            const ui_screen_def_t *target = ui_find_screen(button->target);
            return target != NULL ? target : screen;
        }
    }

    return screen;
}

/**
 * @brief 初始化综合 UI 应用。
 *
 * @retval 0 初始化成功。
 * @retval 1 初始化失败。
 *
 * @details 初始化顺序按依赖排列：LCD framebuffer、离屏缓冲、字体、
 * 触摸输入、设备控制、传感器状态。若中途失败，会释放已经初始化的资源。
 */
int ui_app_init(void)
{
    const char *initial_screen = ui_env_or_default("UI_INITIAL_SCREEN",
                                                  ui_app_initial_screen);
    const ui_screen_def_t *screen = ui_find_screen(initial_screen);

    if(screen == NULL) {
        LOG_ERROR("initial screen not found: %s", initial_screen);
        return 1;
    }

    if(lcd_init() != 0)
        return 1;

    ui_frame_init();

    if(lcd_word_init() != 0) {
        ui_frame_cleanup();
        lcd_uninit();
        return 1;
    }

    if(touch_init() != 0) {
        ui_frame_cleanup();
        lcd_word_uninit();
        lcd_uninit();
        return 1;
    }

    ui_device_init();
    ui_sensor_init();
    current_screen = screen;
    ui_draw_screen(screen);

    return 0;
}

/**
 * @brief 执行一次应用事件更新。
 *
 * @retval 0 更新成功，主循环应继续。
 * @retval 1 当前屏幕无效或触摸读取失败，主循环应退出。
 *
 * @details 先用短超时读取触摸事件，保证按键响应优先；没有触摸事件时
 * 再推进一次传感器非阻塞状态机。这样传感器刷新不会卡住页面按钮响应。
 */
int ui_app_update(void)
{
    touch_event_t event;
    const ui_screen_def_t *next_screen;
    int touch_ret;

    if(current_screen == NULL)
        return 1;

    touch_ret = touch_read_timeout(&event, UI_TOUCH_WAIT_MS);
    if(touch_ret == -5)
        goto poll_sensor;

    if(touch_ret != 0)
        return 1;

    next_screen = ui_handle_touch(current_screen, &event);
    if(next_screen != current_screen) {
        if(ui_screen_uses_sensor(current_screen) &&
           !ui_screen_uses_sensor(next_screen))
            ui_sensor_sync_alarm_beep(0);

        current_screen = next_screen;
        if(ui_screen_uses_sensor(current_screen))
            ui_sensor_request_refresh();
        ui_draw_screen(current_screen);
        return 0;
    }

poll_sensor:
    if(ui_screen_uses_sensor(current_screen) && ui_sensor_poll())
        ui_draw_screen(current_screen);

    return 0;
}

/**
 * @brief 释放综合 UI 应用资源。
 *
 * @details 退出前保存配置、关闭报警、熄灭 LED/蜂鸣器并释放 LCD/字体/
 * 触摸资源。调用顺序与初始化大体相反。
 */
void ui_app_cleanup(void)
{
    touch_uninit();
    ui_settings_save();
    ui_sensor_cleanup();
    ui_device_cleanup();
    lcd_word_uninit();
    ui_frame_cleanup();
    lcd_uninit();
    current_screen = NULL;
}
