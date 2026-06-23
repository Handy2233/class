#ifndef __LCD_H__
#define __LCD_H__

/**
 * @file lcd.h
 * @brief LCD framebuffer 显示接口。
 *
 * 该模块封装 GEC6818 板载 Linux framebuffer (/dev/fb0) 的基本绘制能力：
 * 1. 初始化/释放 framebuffer 映射。
 * 2. 绘制单个像素、清屏、清除矩形区域。
 * 3. 加载 PNG/JPEG/BMP 图片，并按指定矩形缩放显示。
 *
 * @details 坐标约定：
 * - 原点在屏幕左上角。
 * - x 向右递增，范围 [0, LCD_W - 1]。
 * - y 向下递增，范围 [0, LCD_H - 1]。
 * - length 表示横向宽度，width 表示纵向高度。
 *
 * @details 颜色格式：
 * - 使用 0x00RRGGBB，和当前 framebuffer 写入格式保持一致。
 */

/** @brief LCD 物理分辨率。 */
#define LCD_W 800
#define LCD_H 480

/** @brief framebuffer 总字节数：800 * 480 * 4。 */
#define LCD_SIZE (LCD_W * LCD_H * 4)

/** @brief 黑色像素值，常用于清屏或透明背景预混合。 */
#define LCD_BLACK 0x00000000

/**
 * @brief 解码后的图片对象。
 *
 * width/height 是原始图片尺寸，rgba 指向 stb_image 分配的 RGBA 像素。
 * 调用 lcd_pic_load() 成功后，必须调用 lcd_pic_free() 释放 rgba。
 */
typedef struct {
    int width;
    int height;
    unsigned char *rgba;
} lcd_pic_t;

/**
 * @brief 初始化 LCD framebuffer。
 *
 * @retval 0 初始化成功。
 * @retval -1 打开 /dev/fb0 失败。
 * @retval -2 mmap framebuffer 失败。
 */
int lcd_init(void);

/**
 * @brief 在 LCD 上绘制一个像素。
 *
 * @param[in] x 像素横坐标。
 * @param[in] y 像素纵坐标。
 * @param[in] color 0x00RRGGBB 格式颜色。
 *
 *
 * 坐标越界或 LCD 未初始化时函数直接返回。
 */
void lcd_show(int x, int y, unsigned int color);

/**
 * @brief 从文件加载图片到 lcd_pic_t。
 *
 * @param[out] pic 输出图片对象。
 * @param[in] pic_path 图片路径，支持 PNG/JPEG/BMP。
 *
 * @retval 0 加载成功。
 * @retval -1 pic 参数为空。
 * @retval -2 pic_path 参数为空。
 * @retval -3 图片解码失败。
 */
int lcd_pic_load(lcd_pic_t *pic, const char *pic_path);

/**
 * @brief 将已加载图片缩放绘制到指定矩形。
 *
 * @param[in] pic 已由 lcd_pic_load() 加载的图片。
 * @param[in] x 目标矩形左上角横坐标。
 * @param[in] y 目标矩形左上角纵坐标。
 * @param[in] length 目标矩形宽度。
 * @param[in] width 目标矩形高度。
 *
 * @retval 0 绘制成功。
 * @retval -1 LCD 未初始化。
 * @retval -2 图片对象无效。
 * @retval -3 目标尺寸无效。
 */
int lcd_pic_show(const lcd_pic_t *pic, int x, int y, int length, int width);

/**
 * @brief 将已加载图片缩放绘制到指定矩形，并限制实际刷新区域。
 *
 * @param[in] pic 已加载图片。
 * @param[in] x 图片目标矩形左上角横坐标。
 * @param[in] y 图片目标矩形左上角纵坐标。
 * @param[in] length 图片目标矩形宽度。
 * @param[in] width 图片目标矩形高度。
 * @param[in] clip_x 允许刷新的裁剪矩形左上角横坐标。
 * @param[in] clip_y 允许刷新的裁剪矩形左上角纵坐标。
 * @param[in] clip_length 允许刷新的裁剪矩形宽度。
 * @param[in] clip_width 允许刷新的裁剪矩形高度。
 *
 * @retval 0 绘制成功。
 * @retval -1 LCD 未初始化。
 * @retval -2 图片对象无效。
 * @retval -3 图片目标尺寸或裁剪尺寸无效。
 *
 *
 * 该接口适合局部刷新，避免重绘整屏。
 */
int lcd_pic_show_clip(const lcd_pic_t *pic,
                      int x,
                      int y,
                      int length,
                      int width,
                      int clip_x,
                      int clip_y,
                      int clip_length,
                      int clip_width);

/**
 * @brief 释放 lcd_pic_t 中的图片像素。
 *
 * @param[in,out] pic 要释放的图片对象。
 *
 *
 * 函数会清空 rgba/width/height，可安全重复调用。
 */
void lcd_pic_free(lcd_pic_t *pic);

/**
 * @brief 加载并全屏显示图片。
 *
 * @param[in] pic_path 图片路径。
 *
 * @retval 0 显示成功。
 * @retval <0 打开图片、解码或绘制失败。
 */
int lcd_show_pic_full(const char *pic_path);

/**
 * @brief 加载图片并显示到指定矩形。
 *
 * @param[in] pic_path 图片路径。
 * @param[in] x 目标矩形左上角横坐标。
 * @param[in] y 目标矩形左上角纵坐标。
 * @param[in] length 目标矩形宽度。
 * @param[in] width 目标矩形高度。
 *
 * @retval 0 显示成功。
 * @retval -1 LCD 未初始化。
 * @retval -3 目标尺寸无效。
 * @retval -4 图片解码失败。
 * @retval <0 参数无效。
 */
int lcd_show_pic_rect(const char *pic_path, int x, int y, int length, int width);

/** @brief 清空整个屏幕为黑色。 */
void lcd_clear(void);

/**
 * @brief 清空指定矩形区域为黑色。
 *
 * @param[in] x 矩形左上角横坐标。
 * @param[in] y 矩形左上角纵坐标。
 * @param[in] length 矩形宽度。
 * @param[in] width 矩形高度。
 */
void lcd_clear_rect(int x, int y, int length, int width);

/** @brief 释放 framebuffer 映射并关闭 /dev/fb0。 */
void lcd_uninit(void);

/**
 * @brief 获取 framebuffer 映射地址。
 *
 * @retval 非NULL 指向 [LCD_H][LCD_W] 形式的 framebuffer 像素数组。
 * @retval NULL LCD 尚未初始化或已经释放。
 *
 *
 * 该接口主要用于高性能批量绘制，例如离屏缓存整屏 memcpy。
 */
unsigned int (*lcd_get_p(void))[LCD_W];

/**
 * @brief lcd_show_pic() 参数个数分发宏。
 *
 * 用法：
 * - lcd_show_pic("a.png") 等价于 lcd_show_pic_full("a.png")。
 * - lcd_show_pic("a.png", x, y, w, h) 等价于 lcd_show_pic_rect(...)。
 */
#define LCD_SHOW_PIC_SELECT(_1, _2, _3, _4, _5, NAME, ...) NAME
#define lcd_show_pic(...)                                                     \
    LCD_SHOW_PIC_SELECT(__VA_ARGS__,                                          \
                        lcd_show_pic_rect,                                    \
                        lcd_show_pic_need_1_or_5_args,                       \
                        lcd_show_pic_need_1_or_5_args,                       \
                        lcd_show_pic_need_1_or_5_args,                       \
                        lcd_show_pic_full)(__VA_ARGS__)

#endif
