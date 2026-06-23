#include "lcd.h"
#include "log.h"

/**
 * @file lcd.c
 * @brief LCD framebuffer 实现。
 *
 * 本文件直接操作 Linux framebuffer (/dev/fb0)。初始化时通过 mmap
 * 将整块显存映射为二维像素数组 lcd_p，之后所有绘制函数都直接写
 * lcd_p[y][x]。
 *
 * 图片解码使用 stb_image。对外接口统一把图片解码成 RGBA，显示时
 * 再写成 framebuffer 使用的 0x00RRGGBB 格式。
 */

/** @brief 只启用项目实际需要的图片格式，减少 stb_image 编译体积。 */
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_FAILURE_USERMSG
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

/** @brief framebuffer 文件描述符。-1 表示尚未打开或已经关闭。 */
static int lcd_fd = -1;

/** @brief mmap 后的 LCD 像素地址，按 [y][x] 访问。NULL 表示未初始化。 */
static unsigned int (*lcd_p)[LCD_W] = NULL;

/**
 * @brief 将 8 位 R/G/B 分量打包成 LCD 像素格式。
 *
 * @param[in] r 红色分量。
 * @param[in] g 绿色分量。
 * @param[in] b 蓝色分量。
 *
 * @return 0x00RRGGBB 格式像素。
 */
static unsigned int lcd_rgb(unsigned char r, unsigned char g, unsigned char b)
{
    return ((unsigned int)r << 16) |
           ((unsigned int)g << 8) |
           (unsigned int)b;
}

/**
 * @brief 将 RGBA 前景色混合到已有背景像素上。
 *
 * @param[in] bg framebuffer 中已有的背景像素。
 * @param[in] r 前景红色分量。
 * @param[in] g 前景绿色分量。
 * @param[in] b 前景蓝色分量。
 * @param[in] a 前景 alpha 分量。
 *
 * @return 混合后的 0x00RRGGBB 像素。
 *
 *
 * 透明 PNG 显示时使用该函数，保证半透明区域能和屏幕已有内容融合。
 */
static unsigned int lcd_blend(unsigned int bg,
	                              unsigned char r,
	                              unsigned char g,
                              unsigned char b,
                              unsigned char a)
{
    unsigned int bg_r = (bg >> 16) & 0xff;
    unsigned int bg_g = (bg >> 8) & 0xff;
    unsigned int bg_b = bg & 0xff;

    unsigned int out_r = (r * a + bg_r * (255 - a)) / 255;
    unsigned int out_g = (g * a + bg_g * (255 - a)) / 255;
    unsigned int out_b = (b * a + bg_b * (255 - a)) / 255;

    return lcd_rgb(out_r, out_g, out_b);
}

/**
 * @brief 返回两个整数中的较大值。用于裁剪矩形边界。
 *
 * @param[in] a 第一个整数。
 * @param[in] b 第二个整数。
 *
 * @return 两个整数中的较大值。
 */
static int lcd_max_int(int a, int b)
{
    return a > b ? a : b;
}

/**
 * @brief 返回两个整数中的较小值。用于裁剪矩形边界。
 *
 * @param[in] a 第一个整数。
 * @param[in] b 第二个整数。
 *
 * @return 两个整数中的较小值。
 */
static int lcd_min_int(int a, int b)
{
    return a < b ? a : b;
}

/**
 * @brief 填充一个矩形区域。
 *
 * @param[in] x 矩形左上角横坐标，可超出屏幕。
 * @param[in] y 矩形左上角纵坐标，可超出屏幕。
 * @param[in] length 矩形宽度。
 * @param[in] width 矩形高度。
 * @param[in] color 填充颜色，0x00RRGGBB。
 *
 *
 * 函数内部会裁剪到 LCD 可见范围，调用者不需要提前处理越界。
 */
static void lcd_fill_rect(int x, int y, int length, int width, unsigned int color)
{
    if(lcd_p == NULL)
        return;

    if(length <= 0 || width <= 0)
        return;

    long right = (long)x + length;
    long bottom = (long)y + width;

    int dst_x0 = x < 0 ? 0 : x;
    int dst_y0 = y < 0 ? 0 : y;
    int dst_x1 = right > LCD_W ? LCD_W : (int)right;
    int dst_y1 = bottom > LCD_H ? LCD_H : (int)bottom;

    if(dst_x0 >= dst_x1 || dst_y0 >= dst_y1)
        return;

    int dst_x, dst_y;

    for(dst_y = dst_y0; dst_y < dst_y1; dst_y++) {
        for(dst_x = dst_x0; dst_x < dst_x1; dst_x++) {
            lcd_p[dst_y][dst_x] = color;
        }
    }
}

/**
 * @brief 将 RGBA 图片按目标矩形缩放显示，并限制刷新区域。
 *
 * @param[in] x 图片目标矩形左上角横坐标。
 * @param[in] y 图片目标矩形左上角纵坐标。
 * @param[in] length 图片目标矩形宽度。
 * @param[in] width 图片目标矩形高度。
 * @param[in] pic_w 原始图片宽度。
 * @param[in] pic_h 原始图片高度。
 * @param[in] rgba 原始图片 RGBA 像素。
 * @param[in] clip_x 实际允许绘制的裁剪矩形左上角横坐标。
 * @param[in] clip_y 实际允许绘制的裁剪矩形左上角纵坐标。
 * @param[in] clip_length 实际允许绘制的裁剪矩形宽度。
 * @param[in] clip_width 实际允许绘制的裁剪矩形高度。
 *
 *
 * 这里使用最近邻缩放，计算量低，适合 ARM 板直接 framebuffer 绘制。
 * 绘制前会同时考虑图片矩形和裁剪矩形，避免访问屏幕外内存。
 */
static void lcd_show_rgba_scaled_clip(int x,
                                      int y,
                                      int length,
                                      int width,
                                      int pic_w,
                                      int pic_h,
                                      const unsigned char *rgba,
                                      int clip_x,
                                      int clip_y,
                                      int clip_length,
                                      int clip_width)
{
    if(lcd_p == NULL || rgba == NULL)
        return;

    if(length <= 0 || width <= 0 || pic_w <= 0 || pic_h <= 0 ||
       clip_length <= 0 || clip_width <= 0)
        return;

    long right = (long)x + length;
    long bottom = (long)y + width;
    long clip_right = (long)clip_x + clip_length;
    long clip_bottom = (long)clip_y + clip_width;

    int image_x0 = x < 0 ? 0 : x;
    int image_y0 = y < 0 ? 0 : y;
    int image_x1 = right > LCD_W ? LCD_W : (int)right;
    int image_y1 = bottom > LCD_H ? LCD_H : (int)bottom;
    int clip_x0 = clip_x < 0 ? 0 : clip_x;
    int clip_y0 = clip_y < 0 ? 0 : clip_y;
    int clip_x1 = clip_right > LCD_W ? LCD_W : (int)clip_right;
    int clip_y1 = clip_bottom > LCD_H ? LCD_H : (int)clip_bottom;
    int dst_x0 = lcd_max_int(image_x0, clip_x0);
    int dst_y0 = lcd_max_int(image_y0, clip_y0);
    int dst_x1 = lcd_min_int(image_x1, clip_x1);
    int dst_y1 = lcd_min_int(image_y1, clip_y1);

    if(dst_x0 >= dst_x1 || dst_y0 >= dst_y1)
        return;

    int dst_x, dst_y;

    for(dst_y = dst_y0; dst_y < dst_y1; dst_y++) {
        int src_y = (dst_y - y) * pic_h / width;

        for(dst_x = dst_x0; dst_x < dst_x1; dst_x++) {
            int src_x = (dst_x - x) * pic_w / length;
            const unsigned char *px = rgba + (src_y * pic_w + src_x) * 4;
            unsigned char r = px[0];
            unsigned char g = px[1];
            unsigned char b = px[2];
            unsigned char a = px[3];

            if(a == 0)
                continue;

            if(a == 255) {
                lcd_p[dst_y][dst_x] = lcd_rgb(r, g, b);
            } else {
                lcd_p[dst_y][dst_x] = lcd_blend(lcd_p[dst_y][dst_x], r, g, b, a);
            }
        }
    }
}

/**
 * @brief 将 RGBA 图片按目标矩形缩放显示到屏幕，不额外限制刷新区域。
 *
 * @param[in] x 图片目标矩形左上角横坐标。
 * @param[in] y 图片目标矩形左上角纵坐标。
 * @param[in] length 图片目标矩形宽度。
 * @param[in] width 图片目标矩形高度。
 * @param[in] pic_w 原始图片宽度。
 * @param[in] pic_h 原始图片高度。
 * @param[in] rgba 原始图片 RGBA 像素。
 */
static void lcd_show_rgba_scaled(int x,
                                 int y,
                                 int length,
                                 int width,
                                 int pic_w,
                                 int pic_h,
                                 const unsigned char *rgba)
{
    lcd_show_rgba_scaled_clip(x, y, length, width, pic_w, pic_h, rgba,
	                              0, 0, LCD_W, LCD_H);
}

/**
 * @brief 初始化 LCD framebuffer。
 *
 * @retval 0 成功。
 * @retval -1 打开 /dev/fb0 失败。
 * @retval -2 mmap 失败。
 */
int lcd_init(void)
{
    lcd_fd = open("/dev/fb0", O_RDWR);
    if(lcd_fd == -1) {
        LOG_PERROR("open /dev/fb0");
        return -1;
    }

    lcd_p = mmap(NULL, LCD_SIZE,
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED,
                 lcd_fd,
                 0);

    if(lcd_p == MAP_FAILED) {
        LOG_PERROR("mmap");
        close(lcd_fd);
        lcd_fd = -1;
        lcd_p = NULL;
        return -2;
    }
    LOG_INFO("lcd init ok");
    return 0;
}

/**
 * @brief 绘制单个像素。
 *
 * @param[in] x 目标横坐标。
 * @param[in] y 目标纵坐标。
 * @param[in] color 0x00RRGGBB 格式颜色。
 */
void lcd_show(int x, int y, unsigned int color)
{
    if(lcd_p == NULL)
        return;

    if(x < 0 || x >= LCD_W || y < 0 || y >= LCD_H)
        return;

    lcd_p[y][x] = color;
}

/**
 * @brief 解码图片文件。
 *
 * @param[out] pic 输出图片对象。
 * @param[in] pic_path 图片文件路径。
 *
 * @retval 0 成功。
 * @retval -1 pic 为空。
 * @retval -2 pic_path 为空。
 * @retval -3 stb_image 解码失败。
 *
 *
 * stb_image 输出强制为 4 通道 RGBA，便于后续统一处理透明像素。
 */
int lcd_pic_load(lcd_pic_t *pic, const char *pic_path)
{
    if(pic == NULL)
        return -1;

    if(pic_path == NULL)
        return -2;

    int pic_w, pic_h;
    unsigned char *rgba = stbi_load(pic_path, &pic_w, &pic_h, NULL, 4);

    if(rgba == NULL) {
        LOG_ERROR("load image failed: %s", stbi_failure_reason());
        return -3;
    }

    pic->width = pic_w;
    pic->height = pic_h;
    pic->rgba = rgba;

    return 0;
}

/**
 * @brief 显示已解码图片。
 *
 * @param[in] pic 已加载图片。
 * @param[in] x 目标显示矩形左上角横坐标。
 * @param[in] y 目标显示矩形左上角纵坐标。
 * @param[in] length 目标显示矩形宽度。
 * @param[in] width 目标显示矩形高度。
 *
 * @retval 0 成功。
 * @retval -1 LCD 未初始化。
 * @retval -2 图片无效。
 * @retval -3 目标尺寸无效。
 */
int lcd_pic_show(const lcd_pic_t *pic, int x, int y, int length, int width)
{
    if(lcd_p == NULL)
        return -1;

    if(pic == NULL || pic->rgba == NULL)
        return -2;

    if(length <= 0 || width <= 0)
        return -3;

    lcd_show_rgba_scaled(x, y, length, width, pic->width, pic->height, pic->rgba);

    return 0;
}

/**
 * @brief 显示已解码图片，并裁剪实际绘制区域。
 *
 * @param[in] pic 已加载图片。
 * @param[in] x 图片目标显示矩形左上角横坐标。
 * @param[in] y 图片目标显示矩形左上角纵坐标。
 * @param[in] length 图片目标显示矩形宽度。
 * @param[in] width 图片目标显示矩形高度。
 * @param[in] clip_x 裁剪区域左上角横坐标。
 * @param[in] clip_y 裁剪区域左上角纵坐标。
 * @param[in] clip_length 裁剪区域宽度。
 * @param[in] clip_width 裁剪区域高度。
 *
 * @retval 0 成功。
 * @retval -1 LCD 未初始化。
 * @retval -2 图片无效。
 * @retval -3 尺寸无效。
 */
int lcd_pic_show_clip(const lcd_pic_t *pic,
	                      int x,
	                      int y,
                      int length,
                      int width,
                      int clip_x,
                      int clip_y,
                      int clip_length,
                      int clip_width)
{
    if(lcd_p == NULL)
        return -1;

    if(pic == NULL || pic->rgba == NULL)
        return -2;

    if(length <= 0 || width <= 0 || clip_length <= 0 || clip_width <= 0)
        return -3;

    lcd_show_rgba_scaled_clip(x, y, length, width, pic->width, pic->height,
                              pic->rgba, clip_x, clip_y, clip_length, clip_width);

    return 0;
}

/**
 * @brief 释放图片解码内存。
 *
 * @param[in,out] pic 要释放的图片对象。
 */
void lcd_pic_free(lcd_pic_t *pic)
{
    if(pic == NULL)
        return;

    if(pic->rgba != NULL) {
        stbi_image_free(pic->rgba);
        pic->rgba = NULL;
    }

    pic->width = 0;
    pic->height = 0;
}

/**
 * @brief 加载图片并显示到指定矩形。
 *
 * @param[in] pic_path 图片路径。
 * @param[in] x 显示矩形左上角横坐标。
 * @param[in] y 显示矩形左上角纵坐标。
 * @param[in] length 显示矩形宽度。
 * @param[in] width 显示矩形高度。
 *
 * @retval 0 成功。
 * @retval -1 LCD 未初始化。
 * @retval -3 目标尺寸无效。
 * @retval -4 图片解码失败。
 * @retval <0 参数错误。
 */
int lcd_show_pic_rect(const char *pic_path, int x, int y, int length, int width)
{
    if(lcd_p == NULL)
        return -1;

    if(length <= 0 || width <= 0)
        return -3;

    lcd_pic_t pic = {0, 0, NULL};
    int ret = lcd_pic_load(&pic, pic_path);

    if(ret != 0)
        return ret == -3 ? -4 : ret;

    ret = lcd_pic_show(&pic, x, y, length, width);
    lcd_pic_free(&pic);

    return ret;
}

/**
 * @brief 加载图片并全屏显示。
 *
 * @param[in] pic_path 图片路径。
 *
 * @retval 0 成功。
 * @retval <0 加载或显示失败。
 */
int lcd_show_pic_full(const char *pic_path)
{
    return lcd_show_pic_rect(pic_path, 0, 0, LCD_W, LCD_H);
}

/** @brief 清空整屏为黑色。 */
void lcd_clear(void)
{
    if(lcd_p == NULL)
        return;

    lcd_fill_rect(0, 0, LCD_W, LCD_H, LCD_BLACK);
    LOG_INFO("lcd clear ok");
}

/**
 * @brief 清空指定矩形区域。
 *
 * @param[in] x 要清空矩形的左上角横坐标。
 * @param[in] y 要清空矩形的左上角纵坐标。
 * @param[in] length 要清空矩形的宽度。
 * @param[in] width 要清空矩形的高度。
 */
void lcd_clear_rect(int x, int y, int length, int width)
{
    lcd_fill_rect(x, y, length, width, LCD_BLACK);
}

/** @brief 释放 framebuffer 映射并关闭设备。 */
void lcd_uninit(void)
{
    if(lcd_p != NULL) {
        munmap(lcd_p, LCD_SIZE);
        lcd_p = NULL;
    }

    if(lcd_fd != -1) {
        close(lcd_fd);
        lcd_fd = -1;
    }
}

/**
 * @brief 获取 framebuffer 映射地址。
 *
 * @retval 非NULL 已初始化时返回 framebuffer 地址。
 * @retval NULL 未初始化或已经释放。
 */
unsigned int (*lcd_get_p(void))[LCD_W]
{
    return lcd_p;
}
