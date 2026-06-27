#ifndef UI_APP_DATA_H
#define UI_APP_DATA_H

/**
 * @file ui_app_data.h
 * @brief LCD 触摸前端静态页面定义。
 *
 * @details 本文件只保存静态页面元数据，不包含绘制算法和触摸处理逻辑。
 * 坐标单位均为 LCD 像素，原点在屏幕左上角，x 向右递增，y 向下递增。
 * ui_app.c 根据这些定义绘制标题、按钮，并通过 target 字段完成页面跳转。
 */

/**
 * @brief 页面中的静态文字标签。
 *
 * @details 标签用于标题、页面说明等不需要点击的文字。颜色使用 0x00RRGGBB
 * 格式，和 lcd_show() 接受的颜色格式保持一致。
 */
typedef struct {
    /** @brief UTF-8 文本内容。 */
    const char *text;

    /** @brief 文本左上角 x 坐标。 */
    int x;

    /** @brief 文本左上角 y 坐标。 */
    int y;

    /** @brief 文本颜色，格式为 0x00RRGGBB。 */
    unsigned int color;
} ui_label_def_t;

/**
 * @brief 页面按钮定义。
 *
 * @details 按钮既描述可视样式，也描述点击后的目标页面。target 为
 * "album" 时由 ui_app.c 进入相册浏览流程；其他值会在 ui_app_screens
 * 中查找同名页面。
 */
typedef struct {
    /** @brief 按钮内部标识，便于调试和后续扩展。 */
    const char *id;

    /** @brief 按钮主标题。 */
    const char *text;

    /** @brief 按钮副标题，可为空字符串。 */
    const char *subtitle;

    /** @brief 按钮左上角 x 坐标。 */
    int x;

    /** @brief 按钮左上角 y 坐标。 */
    int y;

    /** @brief 按钮宽度。 */
    int w;

    /** @brief 按钮高度。 */
    int h;

    /** @brief 按钮背景色，格式为 0x00RRGGBB。 */
    unsigned int bg;

    /** @brief 按钮主文字颜色。 */
    unsigned int fg;

    /** @brief 按钮强调色，用于左侧色条、箭头和边线。 */
    unsigned int accent;

    /** @brief 圆角半径，单位像素。 */
    int radius;

    /** @brief 点击后跳转的目标页面 id 或特殊目标。 */
    const char *target;
} ui_button_def_t;

/**
 * @brief 一个完整页面的静态定义。
 *
 * @details 页面本身只保存标签和按钮数组。监测页、设备页、历史报警页
 * 的动态内容由 ui_app.c 在静态元素绘制完成后额外叠加。
 */
typedef struct {
    /** @brief 页面 id，按钮 target 会通过它查找页面。 */
    const char *id;

    /** @brief 页面基础背景色。 */
    unsigned int background;

    /** @brief 静态标签数组。 */
    const ui_label_def_t *labels;

    /** @brief 静态标签数量。 */
    int label_count;

    /** @brief 按钮数组。 */
    const ui_button_def_t *buttons;

    /** @brief 按钮数量。 */
    int button_count;
} ui_screen_def_t;

static const ui_label_def_t ui_screen_0_labels[] = {
    {"SMART PANEL", 54, 30, 0x002874ff},
    {"开发板触摸控制台", 54, 72, 0x00172033},
};

static const ui_button_def_t ui_screen_0_buttons[] = {
    {"open_album", "01  电子相册", "左右滑动切换图片", 54, 144, 692, 88, 0x00ffffff, 0x00172033, 0x002874ff, 28, "album"},
    {"open_monitor", "02  数据监测", "GY-39 环境数据 / Z-MQ-01 烟雾浓度", 54, 252, 692, 88, 0x00ffffff, 0x00172033, 0x0000a7b5, 28, "monitor"},
    {"open_device", "03  LED灯及蜂鸣器控制", "触摸控制 D7-D10 与蜂鸣器", 54, 360, 692, 88, 0x00ffffff, 0x00172033, 0x00ff7a45, 28, "device"},
};

static const ui_label_def_t ui_screen_1_labels[] = {
    {"DATA MONITOR", 54, 30, 0x0000a7b5},
    {"数据监测", 54, 72, 0x00172033},
};

static const ui_button_def_t ui_screen_1_buttons[] = {
    {"monitor_back", "返回首页", "", 612, 36, 132, 54, 0x00ffffff, 0x00172033, 0x0000a7b5, 24, "home"},
};

static const ui_label_def_t ui_screen_history_labels[] = {
    {"ALARM HISTORY", 54, 30, 0x00d92d20},
    {"历史报警", 54, 72, 0x00172033},
};

static const ui_button_def_t ui_screen_history_buttons[] = {
    {"history_back", "返回监测", "", 612, 36, 132, 54, 0x00ffffff, 0x00172033, 0x00d92d20, 24, "monitor"},
};

static const ui_label_def_t ui_screen_2_labels[] = {
    {"DEVICE PANEL", 54, 30, 0x00ff7a45},
    {"LED灯及蜂鸣器控制", 54, 72, 0x00172033},
    {"触摸开关已接入 LED / 蜂鸣器", 138, 386, 0x00708092},
};

static const ui_button_def_t ui_screen_2_buttons[] = {
    {"device_back", "返回首页", "", 612, 36, 132, 54, 0x00ffffff, 0x00172033, 0x00ff7a45, 24, "home"},
};

static const ui_screen_def_t ui_app_screens[] = {
    {"home", 0x00f5f8fb, ui_screen_0_labels, (int)(sizeof(ui_screen_0_labels) / sizeof(ui_screen_0_labels[0])), ui_screen_0_buttons, (int)(sizeof(ui_screen_0_buttons) / sizeof(ui_screen_0_buttons[0]))},
    {"monitor", 0x00f5fbff, ui_screen_1_labels, (int)(sizeof(ui_screen_1_labels) / sizeof(ui_screen_1_labels[0])), ui_screen_1_buttons, (int)(sizeof(ui_screen_1_buttons) / sizeof(ui_screen_1_buttons[0]))},
    {"alarm_history", 0x00fff8f8, ui_screen_history_labels, (int)(sizeof(ui_screen_history_labels) / sizeof(ui_screen_history_labels[0])), ui_screen_history_buttons, (int)(sizeof(ui_screen_history_buttons) / sizeof(ui_screen_history_buttons[0]))},
    {"device", 0x00fff8f4, ui_screen_2_labels, (int)(sizeof(ui_screen_2_labels) / sizeof(ui_screen_2_labels[0])), ui_screen_2_buttons, (int)(sizeof(ui_screen_2_buttons) / sizeof(ui_screen_2_buttons[0]))},
};

static const int ui_app_screen_count = (int)(sizeof(ui_app_screens) / sizeof(ui_app_screens[0]));
static const char *ui_app_initial_screen = "home";

#endif
