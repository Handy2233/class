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

static int touch_fd = -1;
static int touch_x_min = 0;
static int touch_x_max = LCD_W - 1;
static int touch_y_min = 0;
static int touch_y_max = LCD_H - 1;

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

static int touch_point_in_rect(int px, int py, int x, int y, int length, int width)
{
    long right = (long)x + length;
    long bottom = (long)y + width;

    return px >= x && px < right && py >= y && py < bottom;
}

static long touch_now_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return tv.tv_sec * 1000L + tv.tv_usec / 1000L;
}

static int touch_distance_sq(int x0, int y0, int x1, int y1)
{
    int dx = x0 - x1;
    int dy = y0 - y1;

    return dx * dx + dy * dy;
}

typedef struct {
    int x;
    int y;
    int length;
    int width;
} touch_rect_t;

typedef struct {
    int length;
    int width;
    unsigned int *pixels;
} touch_pic_cache_t;

typedef struct {
    unsigned int *pixels;
} touch_frame_t;

static unsigned int touch_rgb(unsigned char r, unsigned char g, unsigned char b)
{
    return ((unsigned int)r << 16) |
           ((unsigned int)g << 8) |
           (unsigned int)b;
}

static unsigned int touch_blend_black(unsigned char r,
                                      unsigned char g,
                                      unsigned char b,
                                      unsigned char a)
{
    if(a == 255)
        return touch_rgb(r, g, b);

    return touch_rgb(r * a / 255, g * a / 255, b * a / 255);
}

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

static void touch_cache_free(touch_pic_cache_t *cache)
{
    if(cache == NULL)
        return;

    free(cache->pixels);
    cache->pixels = NULL;
    cache->length = 0;
    cache->width = 0;
}

static int touch_frame_init(touch_frame_t *frame)
{
    if(frame == NULL)
        return -1;

    frame->pixels = malloc(LCD_SIZE);
    if(frame->pixels == NULL)
        return -2;

    return 0;
}

static void touch_frame_free(touch_frame_t *frame)
{
    if(frame == NULL)
        return;

    free(frame->pixels);
    frame->pixels = NULL;
}

static void touch_frame_clear(touch_frame_t *frame)
{
    if(frame == NULL || frame->pixels == NULL)
        return;

    memset(frame->pixels, 0, LCD_SIZE);
}

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

static void touch_frame_flush(const touch_frame_t *frame)
{
    if(frame == NULL || frame->pixels == NULL)
        return;

    unsigned int (*fb)[LCD_W] = lcd_get_p();
    if(fb == NULL)
        return;

    memcpy(fb, frame->pixels, LCD_SIZE);
}

static void touch_show_cached_pic(touch_frame_t *frame,
                                  const touch_pic_cache_t *cache,
                                  int img_x,
                                  int img_y)
{
    touch_frame_clear(frame);
    touch_frame_draw_pic(frame, cache, img_x, img_y);
    touch_frame_flush(frame);
}

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

static int touch_process_input_event(const struct input_event *ev, touch_event_t *event)
{
    static int raw_x = 0;
    static int raw_y = 0;
    static int has_pos = 0;
    static int pressed = 0;
    static int last_pressed = 0;
    static int pos_changed = 0;

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

    if(ev->type == EV_KEY && ev->code == BTN_TOUCH) {
        pressed = ev->value != 0;
        return 0;
    }

    if(ev->type != EV_SYN || ev->code != SYN_REPORT)
        return 0;

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

int touch_read(touch_event_t *event)
{
    if(event == NULL)
        return -1;

    if(touch_fd == -1)
        return -2;

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

            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(touch_fd, &read_set);

            if(select(touch_fd + 1, &read_set, NULL, NULL, NULL) == -1) {
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

void touch_uninit(void)
{
    if(touch_fd != -1) {
        close(touch_fd);
        touch_fd = -1;
    }
}

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

            if(touch_distance_sq(event.x, event.y, down_x, down_y) >
               tap_move_limit * tap_move_limit)
                tap_moved = 1;

            pic_x = event.x - offset_x;
            pic_y = event.y - offset_y;

            if(pic_x != old_x || pic_y != old_y)
                touch_show_cached_pic(&frame, &cache, pic_x, pic_y);
        } else if(event.type == TOUCH_EVENT_UP) {
            if(dragging && !tap_moved && !skip_tap_record) {
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

int lcd_drag_pic_full(const char *pic_path)
{
    return lcd_drag_pic_rect(pic_path, 0, 0, LCD_W, LCD_H);
}
