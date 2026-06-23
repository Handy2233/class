#ifndef __TOUCH_H__
#define __TOUCH_H__

/**
 * @file touch.h
 * @brief 触摸屏输入接口。
 *
 * 该模块封装 Linux input event 设备，向上层提供简化后的触摸事件：
 * - TOUCH_EVENT_DOWN: 手指按下。
 * - TOUCH_EVENT_MOVE: 手指移动。
 * - TOUCH_EVENT_UP: 手指抬起。
 *
 * 坐标会根据 input 设备上报的 ABS_X/ABS_Y 范围映射到 LCD 坐标系：
 * - x 范围 [0, LCD_W - 1]。
 * - y 范围 [0, LCD_H - 1]。
 */

/** @brief 默认触摸输入设备路径，可在编译时通过 -DTOUCH_DEV_PATH=... 覆盖。 */
#ifndef TOUCH_DEV_PATH
#define TOUCH_DEV_PATH "/dev/input/event0"
#endif

/** @brief 归一化后的触摸事件类型。 */
typedef enum {
    TOUCH_EVENT_DOWN = 1,
    TOUCH_EVENT_MOVE,
    TOUCH_EVENT_UP
} touch_event_type_t;

/**
 * @brief 单个触摸事件。
 *
 * x/y 是映射后的 LCD 坐标，type 表示按下、移动或抬起。
 */
typedef struct {
    int x;
    int y;
    touch_event_type_t type;
} touch_event_t;

/**
 * @brief 初始化触摸设备。
 *
 * @retval 0 初始化成功。
 * @retval -1 打开 TOUCH_DEV_PATH 失败。
 *
 *
 * 函数会以非阻塞方式打开 input event 设备，并读取 ABS_X/ABS_Y
 * 的硬件取值范围，用于后续坐标缩放。
 */
int touch_init(void);

/**
 * @brief 读取一个归一化触摸事件。
 *
 * @param[out] event 输出事件。
 *
 * @retval 0 成功读取到 DOWN/MOVE/UP 事件。
 * @retval -1 event 参数为空。
 * @retval -2 触摸设备尚未初始化。
 * @retval -3 select 等待失败。
 * @retval -4 read 触摸设备失败。
 *
 *
 * 该函数内部会阻塞等待事件；连续 MOVE 事件会合并，只返回最新位置，
 * 以降低上层绘制压力。
 */
int touch_read(touch_event_t *event);

/** @brief 关闭触摸设备。 */
void touch_uninit(void);

/**
 * @brief 加载图片并允许用户按全屏尺寸拖动。
 *
 * @param[in] pic_path 图片路径。
 *
 * @retval 0 正常结束。
 * @retval -1 显示尺寸无效。
 * @retval -2 触摸初始化失败。
 * @retval -3 图片加载或缓存生成失败。
 * @retval -4 离屏帧缓存初始化失败。
 * @retval <0 触摸读取失败。
 *
 * @note 交互：
 * - 按住图片后拖动，图片会跟随手指移动。
 * - 双击图片会通过动画回到初始位置。
 */
int lcd_drag_pic_full(const char *pic_path);

/**
 * @brief 加载图片并允许用户在指定矩形内拖动。
 *
 * @param[in] pic_path 图片路径。
 * @param[in] x 图片初始显示位置横坐标。
 * @param[in] y 图片初始显示位置纵坐标。
 * @param[in] length 图片显示宽度。
 * @param[in] width 图片显示高度。
 *
 * @retval 0 正常结束。
 * @retval -1 显示尺寸无效。
 * @retval -2 触摸初始化失败。
 * @retval -3 图片加载或缓存生成失败。
 * @retval -4 离屏帧缓存初始化失败。
 * @retval <0 触摸读取失败。
 *
 * @note 交互：
 * - 按住图片后拖动，图片会跟随手指移动。
 * - 双击图片会通过动画回到初始位置。
 */
int lcd_drag_pic_rect(const char *pic_path, int x, int y, int length, int width);

/**
 * @brief lcd_drag_pic() 参数个数分发宏。
 *
 * 用法：
 * - lcd_drag_pic("a.png") 等价于 lcd_drag_pic_full("a.png")。
 * - lcd_drag_pic("a.png", x, y, w, h) 等价于 lcd_drag_pic_rect(...)。
 */
#define LCD_DRAG_PIC_SELECT(_1, _2, _3, _4, _5, NAME, ...) NAME
#define lcd_drag_pic(...)                                                     \
    LCD_DRAG_PIC_SELECT(__VA_ARGS__,                                          \
                        lcd_drag_pic_rect,                                    \
                        lcd_drag_pic_need_1_or_5_args,                       \
                        lcd_drag_pic_need_1_or_5_args,                       \
                        lcd_drag_pic_need_1_or_5_args,                       \
                        lcd_drag_pic_full)(__VA_ARGS__)

#endif
