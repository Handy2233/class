#include "touch.h"
#include "lcd.h"
#include "log.h"

#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

/**
 * @file touch.c
 * @brief 触摸输入和图片拖动实现。
 *
 * 本文件做两件事：
 * 1. 从 Linux input event 设备读取原始事件，并转换成上层容易使用的
 *    TOUCH_EVENT_DOWN / MOVE / UP。
 * 2. 提供一个图片拖动演示接口 lcd_drag_pic()。为了拖动流畅，图片会先
 *    缩放成固定尺寸缓存，拖动时只做内存拷贝和整屏刷新，不重复解码图片。
 */

/** @brief 触摸设备文件描述符。-1 表示尚未打开或已经关闭。 */
static int touch_fd = -1;

/**
 * @brief 触摸硬件 ABS_X/ABS_Y 上报范围。
 *
 * 不同屏幕驱动的原始坐标范围可能不是 0..799 / 0..479，所以初始化时
 * 通过 EVIOCGABS 读取真实范围，再映射到 LCD 坐标系。
 */
static int touch_x_min = 0;
static int touch_x_max = LCD_W - 1;
static int touch_y_min = 0;
static int touch_y_max = LCD_H - 1;

/**
 * @brief 将 input 设备原始坐标缩放到 LCD 坐标范围。
 *
 * @param[in] value 原始坐标值。
 * @param[in] in_min 原始坐标最小值。
 * @param[in] in_max 原始坐标最大值。
 * @param[in] out_max 输出坐标最大值，例如 LCD_W - 1。
 *
 * @return 缩放并限制到 [0, out_max] 的坐标。
 */
static int touch_scale(int value, int in_min, int in_max, int out_max)
{
    if(in_max <= in_min)
        return value;

    long scaled = (long)(value - in_min) * out_max / (in_max - in_min);

    if(scaled < 0)
        return 0;

    if(scaled > out_max)
        return out_max;

    return (int)scaled;
}

/**
 * @brief 读取触摸设备的 ABS_X/ABS_Y 范围。
 *
 * @retval 0 函数执行完成。
 *
 *
 * 如果 ioctl 失败，会保留默认范围，避免初始化直接失败。
 */
static int touch_read_abs_info(void)
{
    struct input_absinfo abs_x;
    struct input_absinfo abs_y;

    if(ioctl(touch_fd, EVIOCGABS(ABS_X), &abs_x) == 0) {
        touch_x_min = abs_x.minimum;
        touch_x_max = abs_x.maximum;
    }

    if(ioctl(touch_fd, EVIOCGABS(ABS_Y), &abs_y) == 0) {
        touch_y_min = abs_y.minimum;
        touch_y_max = abs_y.maximum;
    }

    return 0;
}

/**
 * @brief 判断一个点是否落在矩形内。
 *
 * @param[in] px 点的横坐标。
 * @param[in] py 点的纵坐标。
 * @param[in] x 矩形左上角横坐标。
 * @param[in] y 矩形左上角纵坐标。
 * @param[in] length 矩形宽度。
 * @param[in] width 矩形高度。
 *
 * @retval 1 点在矩形内。
 * @retval 0 点在矩形外。
 */
static int touch_point_in_rect(int px, int py, int x, int y, int length, int width)
{
    long right = (long)x + length;
    long bottom = (long)y + width;

    return px >= x && px < right && py >= y && py < bottom;
}

/**
 * @brief 获取当前时间，单位毫秒。
 *
 * @return 当前时间戳，单位 ms。
 *
 *
 * 用于双击判断，不要求绝对时间准确，只要求前后差值稳定。
 */
static long touch_now_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return tv.tv_sec * 1000L + tv.tv_usec / 1000L;
}

/**
 * @brief 计算两个点的距离平方。
 *
 * @param[in] x0 第一个点的横坐标。
 * @param[in] y0 第一个点的纵坐标。
 * @param[in] x1 第二个点的横坐标。
 * @param[in] y1 第二个点的纵坐标。
 *
 * @return 距离平方。
 *
 *
 * 避免 sqrt，适合只和阈值平方比较的场景。
 */
static int touch_distance_sq(int x0, int y0, int x1, int y1)
{
    int dx = x0 - x1;
    int dy = y0 - y1;

    return dx * dx + dy * dy;
}

/** @brief 屏幕内可见矩形，用于裁剪拖动时超出屏幕的图片。 */
typedef struct {
    int x;
    int y;
    int length;
    int width;
} touch_rect_t;

/**
 * @brief 拖动图片缓存。
 *
 * pixels 保存已经缩放到 length * width 的 0x00RRGGBB 像素。
 * 拖动时直接按行拷贝该缓存，避免每帧重新缩放原图。
 */
typedef struct {
    int length;
    int width;
    unsigned int *pixels;
} touch_pic_cache_t;

/** @brief 离屏帧缓存，用于先画完整一帧，再刷新到 framebuffer。 */
typedef struct {
    unsigned int *pixels;
} touch_frame_t;

/**
 * @brief 将 RGB 分量打包成 LCD 像素格式。
 *
 * @param[in] r 红色分量。
 * @param[in] g 绿色分量。
 * @param[in] b 蓝色分量。
 *
 * @return 0x00RRGGBB 格式像素。
 */
static unsigned int touch_rgb(unsigned char r, unsigned char g, unsigned char b)
{
    return ((unsigned int)r << 16) |
           ((unsigned int)g << 8) |
           (unsigned int)b;
}

/**
 * @brief 将 RGBA 像素预混合到黑色背景。
 *
 * @param[in] r 图片红色分量。
 * @param[in] g 图片绿色分量。
 * @param[in] b 图片蓝色分量。
 * @param[in] a 图片 alpha 分量。
 *
 * @return 0x00RRGGBB 像素。
 *
 *
 * 拖动缓存不保留 alpha，预混合后拖动时只需要 memcpy。
 */
static unsigned int touch_blend_black(unsigned char r,
	                                      unsigned char g,
	                                      unsigned char b,
                                      unsigned char a)
{
    if(a == 255)
        return touch_rgb(r, g, b);

    return touch_rgb(r * a / 255, g * a / 255, b * a / 255);
}

/**
 * @brief 计算图片矩形在 LCD 内的可见区域。
 *
 * @param[in] x 图片当前矩形左上角横坐标。
 * @param[in] y 图片当前矩形左上角纵坐标。
 * @param[in] length 图片当前矩形宽度。
 * @param[in] width 图片当前矩形高度。
 * @param[out] rect 输出可见区域。
 *
 * @retval 1 有可见区域。
 * @retval 0 完全不可见。
 */
static int touch_visible_rect(int x, int y, int length, int width, touch_rect_t *rect)
{
    long right = (long)x + length;
    long bottom = (long)y + width;

    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = right > LCD_W ? LCD_W : (int)right;
    int y1 = bottom > LCD_H ? LCD_H : (int)bottom;

    if(x0 >= x1 || y0 >= y1)
        return 0;

    rect->x = x0;
    rect->y = y0;
    rect->length = x1 - x0;
    rect->width = y1 - y0;

    return 1;
}

/**
 * @brief 加载图片并生成拖动缓存。
 *
 * @param[out] cache 输出图片缓存。
 * @param[in] pic_path 图片路径。
 * @param[in] length 缓存目标宽度。
 * @param[in] width 缓存目标高度。
 *
 * @retval 0 成功。
 * @retval -1 cache 参数为空。
 * @retval -2 目标尺寸无效。
 * @retval -3 图片加载失败。
 * @retval -4 缓存内存分配失败。
 *
 *
 * 这里将图片缩放成固定尺寸缓存。拖动过程中只移动这块缓存，
 * 不再访问原始 RGBA 图片。
 */
static int touch_cache_load(touch_pic_cache_t *cache,
                            const char *pic_path,
                            int length,
                            int width)
{
    if(cache == NULL)
        return -1;

    if(length <= 0 || width <= 0)
        return -2;

    lcd_pic_t pic = {0, 0, NULL};
    int ret = lcd_pic_load(&pic, pic_path);

    if(ret != 0)
        return -3;

    cache->pixels = malloc((size_t)length * width * sizeof(unsigned int));
    if(cache->pixels == NULL) {
        lcd_pic_free(&pic);
        return -4;
    }

    cache->length = length;
    cache->width = width;

    int x, y;

    for(y = 0; y < width; y++) {
        int src_y = y * pic.height / width;

        for(x = 0; x < length; x++) {
            int src_x = x * pic.width / length;
            const unsigned char *px = pic.rgba + (src_y * pic.width + src_x) * 4;

            cache->pixels[y * length + x] = touch_blend_black(px[0], px[1], px[2], px[3]);
        }
    }

    lcd_pic_free(&pic);

    return 0;
}

/**
 * @brief 释放拖动图片缓存。
 *
 * @param[in,out] cache 要释放的缓存。
 */
static void touch_cache_free(touch_pic_cache_t *cache)
{
    if(cache == NULL)
        return;

    free(cache->pixels);
    cache->pixels = NULL;
    cache->length = 0;
    cache->width = 0;
}

/**
 * @brief 初始化离屏帧缓存。
 *
 * @param[out] frame 输出帧缓存。
 *
 * @retval 0 成功。
 * @retval -1 frame 参数为空。
 * @retval -2 内存分配失败。
 */
static int touch_frame_init(touch_frame_t *frame)
{
    if(frame == NULL)
        return -1;

    frame->pixels = malloc(LCD_SIZE);
    if(frame->pixels == NULL)
        return -2;

    return 0;
}

/**
 * @brief 释放离屏帧缓存。
 *
 * @param[in,out] frame 要释放的帧缓存。
 */
static void touch_frame_free(touch_frame_t *frame)
{
    if(frame == NULL)
        return;

    free(frame->pixels);
    frame->pixels = NULL;
}

/**
 * @brief 清空离屏帧缓存为黑色。
 *
 * @param[in,out] frame 要清空的帧缓存。
 */
static void touch_frame_clear(touch_frame_t *frame)
{
    if(frame == NULL || frame->pixels == NULL)
        return;

    memset(frame->pixels, 0, LCD_SIZE);
}

/**
 * @brief 将图片缓存按指定位置画到离屏帧缓存。
 *
 * @param[in,out] frame 目标离屏帧缓存。
 * @param[in] cache 源图片缓存。
 * @param[in] img_x 图片左上角在屏幕坐标系中的横坐标。
 * @param[in] img_y 图片左上角在屏幕坐标系中的纵坐标。
 *
 *
 * 图片可能被拖出屏幕，所以先计算可见区域，再按行 memcpy。
 */
static void touch_frame_draw_pic(const touch_frame_t *frame,
                                 const touch_pic_cache_t *cache,
                                 int img_x,
                                 int img_y)
{
    if(frame == NULL || frame->pixels == NULL ||
       cache == NULL || cache->pixels == NULL)
        return;

    touch_rect_t rect = {0, 0, 0, 0};
    if(!touch_visible_rect(img_x, img_y, cache->length, cache->width, &rect))
        return;

    int y;

    for(y = 0; y < rect.width; y++) {
        int dst_y = rect.y + y;
        int src_y = dst_y - img_y;
        int src_x = rect.x - img_x;

        memcpy(&frame->pixels[dst_y * LCD_W + rect.x],
               &cache->pixels[src_y * cache->length + src_x],
               (size_t)rect.length * sizeof(unsigned int));
    }
}

/**
 * @brief 将离屏帧缓存刷新到 LCD framebuffer。
 *
 * @param[in] frame 已绘制好的离屏帧缓存。
 */
static void touch_frame_flush(const touch_frame_t *frame)
{
    if(frame == NULL || frame->pixels == NULL)
        return;

    unsigned int (*fb)[LCD_W] = lcd_get_p();
    if(fb == NULL)
        return;

    memcpy(fb, frame->pixels, LCD_SIZE);
}

/**
 * @brief 显示一张已缓存图片。
 *
 * @param[in,out] frame 离屏帧缓存。
 * @param[in] cache 图片缓存。
 * @param[in] img_x 图片显示位置横坐标。
 * @param[in] img_y 图片显示位置纵坐标。
 */
static void touch_show_cached_pic(touch_frame_t *frame,
	                                  const touch_pic_cache_t *cache,
	                                  int img_x,
                                  int img_y)
{
    touch_frame_clear(frame);
    touch_frame_draw_pic(frame, cache, img_x, img_y);
    touch_frame_flush(frame);
}

/**
 * @brief 将图片从一个位置平滑移动到另一个位置。
 *
 * @param[in,out] frame 离屏帧缓存。
 * @param[in] cache 图片缓存。
 * @param[in] from_x 起点横坐标。
 * @param[in] from_y 起点纵坐标。
 * @param[in] to_x 终点横坐标。
 * @param[in] to_y 终点纵坐标。
 *
 *
 * 使用 smoothstep 风格的缓入缓出曲线，双击复位时视觉更自然。
 */
static void touch_animate_cached_pic(touch_frame_t *frame,
                                     const touch_pic_cache_t *cache,
                                     int from_x,
                                     int from_y,
                                     int to_x,
                                     int to_y)
{
    const int frames = 12;
    const int frame_delay_us = 16000;
    long denom = (long)frames * frames * frames;
    int i;

    for(i = 1; i <= frames; i++) {
        long progress = (long)3 * frames * i * i - (long)2 * i * i * i;
        int img_x = from_x + (int)((long)(to_x - from_x) * progress / denom);
        int img_y = from_y + (int)((long)(to_y - from_y) * progress / denom);

        touch_show_cached_pic(frame, cache, img_x, img_y);
        usleep(frame_delay_us);
    }

    touch_show_cached_pic(frame, cache, to_x, to_y);
}

/**
 * @brief 初始化触摸设备。
 *
 * @retval 0 成功。
 * @retval -1 打开 TOUCH_DEV_PATH 失败。
 *
 *
 * 重复调用会直接返回成功，不会重复打开设备。
 */
int touch_init(void)
{
    if(touch_fd != -1)
        return 0;

    touch_fd = open(TOUCH_DEV_PATH, O_RDONLY | O_NONBLOCK);
    if(touch_fd == -1) {
        LOG_PERROR("open touch");
        return -1;
    }

    touch_read_abs_info();
    LOG_INFO("touch init ok");

    return 0;
}

/**
 * @brief 将一条 Linux input_event 转换成上层触摸事件。
 *
 * @param[in] ev 原始 input event。
 * @param[out] event 输出归一化触摸事件。
 *
 * @retval 1 成功生成一个 TOUCH_EVENT_DOWN/MOVE/UP。
 * @retval 0 当前 input event 只是坐标、按键或同步中间状态，暂不输出。
 *
 * @details 状态机说明：
 * - EV_ABS 更新原始坐标或压力。
 * - EV_KEY/BTN_TOUCH 更新按下状态。
 * - EV_SYN/SYN_REPORT 表示一组事件结束，此时根据 pressed/last_pressed
 *   推导 DOWN、MOVE、UP。
 */
static int touch_process_input_event(const struct input_event *ev, touch_event_t *event)
{
    static int raw_x = 0;
    static int raw_y = 0;
    static int has_pos = 0;
    static int pressed = 0;
    static int last_pressed = 0;
    static int pos_changed = 0;

    /** @brief 坐标和压力事件只更新内部状态，等 SYN_REPORT 到来再统一产出事件。 */
    if(ev->type == EV_ABS) {
        if(ev->code == ABS_X) {
            raw_x = ev->value;
            has_pos = 1;
            pos_changed = 1;
        } else if(ev->code == ABS_Y) {
            raw_y = ev->value;
            has_pos = 1;
            pos_changed = 1;
        } else if(ev->code == ABS_PRESSURE) {
            pressed = ev->value > 0;
        }

        return 0;
    }

    /** @brief 部分驱动通过 BTN_TOUCH 表示按下/抬起。 */
    if(ev->type == EV_KEY && ev->code == BTN_TOUCH) {
        pressed = ev->value != 0;
        return 0;
    }

    if(ev->type != EV_SYN || ev->code != SYN_REPORT)
        return 0;

    /**
     * @brief 兼容只上报坐标变化的触摸驱动。
     *
     * @details 某些触摸驱动不明确上报 BTN_TOUCH/ABS_PRESSURE，只持续上报坐标。
     * 如果坐标变化发生在非按下状态，这里把它视为一次按下。
     */
    if(pos_changed && !pressed && !last_pressed)
        pressed = 1;

    if(!has_pos) {
        pos_changed = 0;
        last_pressed = pressed;
        return 0;
    }

    event->x = touch_scale(raw_x, touch_x_min, touch_x_max, LCD_W - 1);
    event->y = touch_scale(raw_y, touch_y_min, touch_y_max, LCD_H - 1);

    if(pressed && !last_pressed) {
        if(!pos_changed)
            return 0;

        event->type = TOUCH_EVENT_DOWN;
    } else if(pressed && pos_changed) {
        event->type = TOUCH_EVENT_MOVE;
    } else if(!pressed && last_pressed) {
        event->type = TOUCH_EVENT_UP;
    } else {
        pos_changed = 0;
        last_pressed = pressed;
        return 0;
    }

    pos_changed = 0;
    last_pressed = pressed;

    return 1;
}

/**
 * @brief 读取一个归一化触摸事件。
 *
 * @param[out] event 输出事件。
 *
 * @retval 0 成功读取事件。
 * @retval -1 event 参数为空。
 * @retval -2 触摸设备未初始化。
 * @retval -3 select 失败。
 * @retval -4 read 失败。
 *
 *
 * 设备以非阻塞方式打开。函数会尽量一次读完内核缓冲区：
 * - DOWN/UP 事件立即返回，保证点击边界清晰。
 * - MOVE 事件只保留最后一个，避免上层绘制追赶大量过期位置。
 */
static int touch_read_internal(touch_event_t *event, int timeout_ms)
{
    long deadline_ms = 0;

    if(event == NULL)
        return -1;

    if(touch_fd == -1)
        return -2;

    if(timeout_ms >= 0)
        deadline_ms = touch_now_ms() + timeout_ms;

    touch_event_t latest_move;
    int has_latest_move = 0;

    while(1) {
        struct input_event ev;
        ssize_t n = read(touch_fd, &ev, sizeof(ev));

        if(n == sizeof(ev)) {
            touch_event_t current;
            int has_event = touch_process_input_event(&ev, &current);

            if(!has_event)
                continue;

            if(current.type == TOUCH_EVENT_DOWN || current.type == TOUCH_EVENT_UP) {
                *event = current;
                return 0;
            }

            latest_move = current;
            has_latest_move = 1;
            continue;
        }

        if(n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if(has_latest_move) {
                *event = latest_move;
                return 0;
            }

            /** @brief 没有可返回事件时阻塞等待下一批 input event。 */
            fd_set read_set;
            struct timeval timeout;
            struct timeval *timeout_ptr = NULL;

            FD_ZERO(&read_set);
            FD_SET(touch_fd, &read_set);

            if(timeout_ms >= 0) {
                long remain_ms = deadline_ms - touch_now_ms();

                if(remain_ms <= 0)
                    return -5;

                timeout.tv_sec = remain_ms / 1000;
                timeout.tv_usec = (remain_ms % 1000) * 1000;
                timeout_ptr = &timeout;
            }

            int ready = select(touch_fd + 1, &read_set, NULL, NULL,
                               timeout_ptr);
            if(ready == 0)
                return -5;

            if(ready == -1) {
                if(errno == EINTR)
                    continue;

                LOG_PERROR("select touch");
                return -3;
            }

            continue;
        }

        if(n == -1 && errno == EINTR)
            continue;

        if(n != sizeof(ev)) {
            LOG_PERROR("read touch");
            return -4;
        }
    }
}

/**
 * @brief 阻塞读取一个触摸事件。
 *
 * @param[out] event 输出触摸事件。
 *
 * @retval 0 成功读取到 DOWN/MOVE/UP 事件。
 * @retval <0 touch_read_internal() 返回的错误码。
 */
int touch_read(touch_event_t *event)
{
    return touch_read_internal(event, -1);
}

/**
 * @brief 在指定超时时间内读取一个触摸事件。
 *
 * @param[out] event 输出触摸事件。
 * @param[in] timeout_ms 超时时间，单位 ms；0 表示只检查当前是否有事件。
 *
 * @retval 0 成功读取到 DOWN/MOVE/UP 事件。
 * @retval -5 超时未读取到完整触摸事件。
 * @retval <0 其他触摸读取错误。
 */
int touch_read_timeout(touch_event_t *event, int timeout_ms)
{
    return touch_read_internal(event, timeout_ms);
}

/**
 * @brief 关闭触摸设备。
 *
 *
 * 关闭后 touch_fd 重置为 -1，后续可再次 touch_init()。
 */
void touch_uninit(void)
{
    if(touch_fd != -1) {
        close(touch_fd);
        touch_fd = -1;
    }
}

/**
 * @brief 加载图片并允许用户拖动。
 *
 * @param[in] pic_path 图片路径。
 * @param[in] x 初始显示位置横坐标。
 * @param[in] y 初始显示位置纵坐标。
 * @param[in] length 图片显示宽度。
 * @param[in] width 图片显示高度。
 *
 * @retval 0 正常结束。
 * @retval -1 尺寸无效。
 * @retval -2 触摸初始化失败。
 * @retval -3 图片缓存加载失败。
 * @retval -4 离屏帧缓存初始化失败。
 * @retval <0 touch_read() 失败。
 *
 * @note 交互规则：
 * - 按下点必须在图片当前矩形内，才进入拖动状态。
 * - MOVE 时图片跟随手指移动。
 * - 双击图片会动画回到初始位置。
 */
int lcd_drag_pic_rect(const char *pic_path, int x, int y, int length, int width)
{
    if(length <= 0 || width <= 0)
        return -1;

    if(touch_init() != 0)
        return -2;

    touch_pic_cache_t cache = {0, 0, NULL};
    int ret = touch_cache_load(&cache, pic_path, length, width);

    if(ret != 0) {
        touch_uninit();
        return -3;
    }

    touch_frame_t frame = {NULL};
    ret = touch_frame_init(&frame);

    if(ret != 0) {
        touch_cache_free(&cache);
        touch_uninit();
        return -4;
    }

    int pic_x = x;
    int pic_y = y;
    int dragging = 0;
    int offset_x = 0;
    int offset_y = 0;
    int down_x = 0;
    int down_y = 0;
    int tap_moved = 0;
    int skip_tap_record = 0;
    int last_tap_valid = 0;
    int last_tap_x = 0;
    int last_tap_y = 0;
    long last_tap_ms = 0;

    const int tap_move_limit = 20;
    const int double_tap_limit = 45;
    const long double_tap_ms = 350;

    touch_show_cached_pic(&frame, &cache, pic_x, pic_y);

    while(1) {
        touch_event_t event;
        ret = touch_read(&event);

        if(ret != 0)
            break;

        if(event.type == TOUCH_EVENT_DOWN) {
            if(touch_point_in_rect(event.x, event.y, pic_x, pic_y, length, width)) {
                long now_ms = touch_now_ms();

                /**
                 * @brief 判断本次按下是否构成双击。
                 *
                 * @details 双击需要同时满足两个条件：两次按下的时间间隔
                 * 小于 double_tap_ms；两次按下位置距离小于 double_tap_limit。
                 */
                if(last_tap_valid &&
                   now_ms - last_tap_ms <= double_tap_ms &&
                   touch_distance_sq(event.x, event.y, last_tap_x, last_tap_y) <=
                       double_tap_limit * double_tap_limit) {
                    int old_x = pic_x;
                    int old_y = pic_y;

                    pic_x = x;
                    pic_y = y;
                    dragging = 0;
                    skip_tap_record = 1;
                    last_tap_valid = 0;

                    if(old_x != pic_x || old_y != pic_y) {
                        touch_animate_cached_pic(&frame, &cache,
                                                 old_x, old_y, pic_x, pic_y);
                    } else {
                        touch_show_cached_pic(&frame, &cache, pic_x, pic_y);
                    }

                    continue;
                }

                /** @brief 记录手指与图片左上角的偏移，MOVE 时用它保持跟手位置。 */
                dragging = 1;
                offset_x = event.x - pic_x;
                offset_y = event.y - pic_y;
                down_x = event.x;
                down_y = event.y;
                tap_moved = 0;
                skip_tap_record = 0;
            }
        } else if(event.type == TOUCH_EVENT_MOVE && dragging) {
            int old_x = pic_x;
            int old_y = pic_y;

            /** @brief 超过轻微抖动范围后，本次触摸不再记为 tap。 */
            if(touch_distance_sq(event.x, event.y, down_x, down_y) >
               tap_move_limit * tap_move_limit)
                tap_moved = 1;

            pic_x = event.x - offset_x;
            pic_y = event.y - offset_y;

            if(pic_x != old_x || pic_y != old_y)
                touch_show_cached_pic(&frame, &cache, pic_x, pic_y);
        } else if(event.type == TOUCH_EVENT_UP) {
            if(dragging && !tap_moved && !skip_tap_record) {
                /** @brief 记录一次有效 tap，供下一次 DOWN 做双击判断。 */
                last_tap_valid = 1;
                last_tap_x = event.x;
                last_tap_y = event.y;
                last_tap_ms = touch_now_ms();
            }

            dragging = 0;
            skip_tap_record = 0;
        }
    }

    touch_frame_free(&frame);
    touch_cache_free(&cache);
    touch_uninit();

    return ret;
}

/**
 * @brief 全屏尺寸拖动图片。
 *
 * @param[in] pic_path 图片路径。
 *
 * @retval 0 正常结束。
 * @retval -1 尺寸无效。
 * @retval -2 触摸初始化失败。
 * @retval -3 图片缓存加载失败。
 * @retval -4 离屏帧缓存初始化失败。
 * @retval <0 touch_read() 失败。
 */
int lcd_drag_pic_full(const char *pic_path)
{
    return lcd_drag_pic_rect(pic_path, 0, 0, LCD_W, LCD_H);
}
