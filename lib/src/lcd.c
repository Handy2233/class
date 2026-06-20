#include "lcd.h"
#include "log.h"

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

static int lcd_fd = -1;
static unsigned int (*lcd_p)[LCD_W] = NULL;

static unsigned int lcd_rgb(unsigned char r, unsigned char g, unsigned char b)
{
    return ((unsigned int)r << 16) |
           ((unsigned int)g << 8) |
           (unsigned int)b;
}

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

static int lcd_max_int(int a, int b)
{
    return a > b ? a : b;
}

static int lcd_min_int(int a, int b)
{
    return a < b ? a : b;
}

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

void lcd_show(int x, int y, unsigned int color)
{
    if(lcd_p == NULL)
        return;

    if(x < 0 || x >= LCD_W || y < 0 || y >= LCD_H)
        return;

    lcd_p[y][x] = color;
}

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

int lcd_show_pic_full(const char *pic_path)
{
    return lcd_show_pic_rect(pic_path, 0, 0, LCD_W, LCD_H);
}

void lcd_clear(void)
{
    if(lcd_p == NULL)
        return;

    lcd_fill_rect(0, 0, LCD_W, LCD_H, LCD_BLACK);
    LOG_INFO("lcd clear ok");
}

void lcd_clear_rect(int x, int y, int length, int width)
{
    lcd_fill_rect(x, y, length, width, LCD_BLACK);
}

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

unsigned int (*lcd_get_p(void))[LCD_W]
{
    return lcd_p;
}
