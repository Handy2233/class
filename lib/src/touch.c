#include "touch.h"
#include "lcd.h"
#include "log.h"

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
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

    if(ioctl(touch_fd, EVIOCGABS(ABS_MT_POSITION_X), &abs_x) == 0) {
        touch_x_min = abs_x.minimum;
        touch_x_max = abs_x.maximum;
    }

    if(ioctl(touch_fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs_y) == 0) {
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

typedef struct {
    int x;
    int y;
    int length;
    int width;
} touch_rect_t;

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

static int touch_union_rect(const touch_rect_t *a, const touch_rect_t *b, touch_rect_t *out)
{
    if(a->length <= 0 || a->width <= 0) {
        *out = *b;
        return b->length > 0 && b->width > 0;
    }

    if(b->length <= 0 || b->width <= 0) {
        *out = *a;
        return a->length > 0 && a->width > 0;
    }

    int x0 = a->x < b->x ? a->x : b->x;
    int y0 = a->y < b->y ? a->y : b->y;
    int x1 = a->x + a->length > b->x + b->length ?
             a->x + a->length : b->x + b->length;
    int y1 = a->y + a->width > b->y + b->width ?
             a->y + a->width : b->y + b->width;

    out->x = x0;
    out->y = y0;
    out->length = x1 - x0;
    out->width = y1 - y0;

    return 1;
}

static void touch_redraw_dirty(const lcd_pic_t *pic,
                               int old_x,
                               int old_y,
                               int new_x,
                               int new_y,
                               int length,
                               int width)
{
    touch_rect_t old_rect = {0, 0, 0, 0};
    touch_rect_t new_rect = {0, 0, 0, 0};
    touch_rect_t dirty_rect = {0, 0, 0, 0};

    touch_visible_rect(old_x, old_y, length, width, &old_rect);
    touch_visible_rect(new_x, new_y, length, width, &new_rect);

    if(!touch_union_rect(&old_rect, &new_rect, &dirty_rect))
        return;

    lcd_clear_rect(dirty_rect.x, dirty_rect.y, dirty_rect.length, dirty_rect.width);
    lcd_pic_show_clip(pic, new_x, new_y, length, width,
                      dirty_rect.x, dirty_rect.y,
                      dirty_rect.length, dirty_rect.width);
}

int touch_init(void)
{
    if(touch_fd != -1)
        return 0;

    touch_fd = open(TOUCH_DEV_PATH, O_RDONLY);
    if(touch_fd == -1) {
        LOG_PERROR("open touch");
        return -1;
    }

    touch_read_abs_info();
    LOG_INFO("touch init ok");

    return 0;
}

int touch_read(touch_event_t *event)
{
    if(event == NULL)
        return -1;

    if(touch_fd == -1)
        return -2;

    static int raw_x = 0;
    static int raw_y = 0;
    static int has_pos = 0;
    static int pressed = 0;
    static int last_pressed = 0;
    int pos_changed = 0;

    while(1) {
        struct input_event ev;
        ssize_t n = read(touch_fd, &ev, sizeof(ev));

        if(n != sizeof(ev)) {
            LOG_PERROR("read touch");
            return -3;
        }

        if(ev.type == EV_ABS) {
            if(ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) {
                raw_x = ev.value;
                has_pos = 1;
                pos_changed = 1;
            } else if(ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) {
                raw_y = ev.value;
                has_pos = 1;
                pos_changed = 1;
            } else if(ev.code == ABS_PRESSURE) {
                pressed = ev.value > 0;
            } else if(ev.code == ABS_MT_TRACKING_ID) {
                pressed = ev.value >= 0;
            }
        } else if(ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            pressed = ev.value != 0;
        } else if(ev.type == EV_SYN && ev.code == SYN_REPORT) {
            if(pos_changed && !pressed && !last_pressed)
                pressed = 1;

            if(!has_pos) {
                pos_changed = 0;
                last_pressed = pressed;
                continue;
            }

            event->x = touch_scale(raw_x, touch_x_min, touch_x_max, LCD_W - 1);
            event->y = touch_scale(raw_y, touch_y_min, touch_y_max, LCD_H - 1);

            if(pressed && !last_pressed) {
                event->type = TOUCH_EVENT_DOWN;
            } else if(pressed && pos_changed) {
                event->type = TOUCH_EVENT_MOVE;
            } else if(!pressed && last_pressed) {
                event->type = TOUCH_EVENT_UP;
            } else {
                pos_changed = 0;
                last_pressed = pressed;
                continue;
            }

            pos_changed = 0;
            last_pressed = pressed;
            return 0;
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

    lcd_pic_t pic = {0, 0, NULL};
    int ret = lcd_pic_load(&pic, pic_path);

    if(ret != 0) {
        touch_uninit();
        return -3;
    }

    int pic_x = x;
    int pic_y = y;
    int dragging = 0;
    int offset_x = 0;
    int offset_y = 0;

    lcd_clear();
    lcd_pic_show(&pic, pic_x, pic_y, length, width);

    while(1) {
        touch_event_t event;
        ret = touch_read(&event);

        if(ret != 0)
            break;

        if(event.type == TOUCH_EVENT_DOWN) {
            if(touch_point_in_rect(event.x, event.y, pic_x, pic_y, length, width)) {
                dragging = 1;
                offset_x = event.x - pic_x;
                offset_y = event.y - pic_y;
            }
        } else if(event.type == TOUCH_EVENT_MOVE && dragging) {
            int old_x = pic_x;
            int old_y = pic_y;

            pic_x = event.x - offset_x;
            pic_y = event.y - offset_y;

            touch_redraw_dirty(&pic, old_x, old_y, pic_x, pic_y, length, width);
        } else if(event.type == TOUCH_EVENT_UP) {
            dragging = 0;
        }
    }

    lcd_pic_free(&pic);
    touch_uninit();

    return ret;
}

int lcd_drag_pic_full(const char *pic_path)
{
    return lcd_drag_pic_rect(pic_path, 0, 0, LCD_W, LCD_H);
}
