#ifndef __TOUCH_H__
#define __TOUCH_H__

#ifndef TOUCH_DEV_PATH
#define TOUCH_DEV_PATH "/dev/input/event0"
#endif

typedef enum {
    TOUCH_EVENT_DOWN = 1,
    TOUCH_EVENT_MOVE,
    TOUCH_EVENT_UP
} touch_event_type_t;

typedef struct {
    int x;
    int y;
    touch_event_type_t type;
} touch_event_t;

int touch_init(void);
int touch_read(touch_event_t *event);
void touch_uninit(void);

int lcd_drag_pic_full(const char *pic_path);
int lcd_drag_pic_rect(const char *pic_path, int x, int y, int length, int width);

#define LCD_DRAG_PIC_SELECT(_1, _2, _3, _4, _5, NAME, ...) NAME
#define lcd_drag_pic(...)                                                     \
    LCD_DRAG_PIC_SELECT(__VA_ARGS__,                                          \
                        lcd_drag_pic_rect,                                    \
                        lcd_drag_pic_need_1_or_5_args,                       \
                        lcd_drag_pic_need_1_or_5_args,                       \
                        lcd_drag_pic_need_1_or_5_args,                       \
                        lcd_drag_pic_full)(__VA_ARGS__)

#endif
