#include "GEC6818.h"
#include "generated/ui_app_data.h"

#define PIC_DIR "./pic"
#define ALBUM_BACK_X 20
#define ALBUM_BACK_Y 18
#define ALBUM_BACK_W 118
#define ALBUM_BACK_H 48
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

/**
 * @file ts_ui_demo.c
 * @brief TypeScript 配置驱动的轻量 framebuffer UI demo。
 *
 * @details 页面内容来自 frontend/app.ts，经 frontend/build-ui.js 生成
 * frontend/generated/ui_app_data.h。本文件只负责绘制页面和处理触摸跳转。
 */

static int device_led_ready;
static int device_led_count;
static int device_led_state[DEVICE_LED_COUNT];
static int device_beep_state;

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

static void ui_draw_hline(int x, int y, int w, unsigned int color)
{
    int col;

    for(col = 0; col < w; col++)
        lcd_show(x + col, y, color);
}

static void ui_draw_vline(int x, int y, int h, unsigned int color)
{
    int row;

    for(row = 0; row < h; row++)
        lcd_show(x, y + row, color);
}

static void ui_draw_grid(unsigned int color)
{
    int x;
    int y;

    for(x = 0; x < LCD_W; x += 40)
        ui_draw_vline(x, 0, LCD_H, color);

    for(y = 0; y < LCD_H; y += 40)
        ui_draw_hline(0, y, LCD_W, color);
}

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

static int ui_point_in_rect(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

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
                            button->subtitle, 0x00667586);
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

static void ui_draw_toggle(int x, int y, int on, unsigned int accent)
{
    unsigned int track = on ? ui_rgb_mix(accent, 0x00ffffff, 12) : 0x00dbe6f0;
    int knob_x = on ? x + DEVICE_TOGGLE_W - 38 : x + 4;

    ui_fill_round_rect(x, y, DEVICE_TOGGLE_W, DEVICE_TOGGLE_H,
                       DEVICE_TOGGLE_H / 2, track);
    ui_fill_round_rect(knob_x, y + 4, 32, 32, 16, 0x00ffffff);
}

static void ui_draw_status_text(int x, int y, int on, int available)
{
    if(!available) {
        lcd_word_show_color(x, y, "N/A", 0x00909cab);
        return;
    }

    lcd_word_show_color(x, y, on ? "ON" : "OFF",
                        on ? 0x0000a75a : 0x00708092);
}

static void ui_draw_monitor_mock(void)
{
    ui_draw_glass_panel(54, 136, 208, 122, 24, 0x00ffffff, 0x0000a7b5);
    lcd_word_show_color(84, 156, "温度", 0x00485767);
    lcd_word_show_color(84, 205, "26.4 C", 0x0000a7b5);

    ui_draw_glass_panel(296, 136, 208, 122, 24, 0x00ffffff, 0x002874ff);
    lcd_word_show_color(326, 156, "湿度", 0x00485767);
    lcd_word_show_color(326, 205, "58 %", 0x002874ff);

    ui_draw_glass_panel(538, 136, 208, 122, 24, 0x00ffffff, 0x00ff7a45);
    lcd_word_show_color(568, 156, "阈值", 0x00485767);
    lcd_word_show_color(568, 205, "75", 0x00ff7a45);

    ui_draw_glass_panel(54, 292, 692, 74, 24, 0x00ffffff, 0x0000a7b5);
    ui_fill_round_rect(92, 332, 604, 10, 5, 0x00d8e4ef);
    ui_fill_round_rect(92, 332, 366, 10, 5, 0x0000a7b5);
    lcd_word_show_color(88, 305, "实时曲线占位", 0x00485767);
}

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
        int y = 202 + i * 38;
        int available = device_led_ready && i < device_led_count;

        lcd_word_show_color(92, y + 4, led_labels[i], 0x00485767);
        ui_draw_status_text(188, y + 4, device_led_state[i], available);
        ui_draw_toggle(366, y, available && device_led_state[i], 0x002874ff);
    }

    ui_draw_glass_panel(DEVICE_BEEP_CARD_X, DEVICE_BEEP_CARD_Y,
                        DEVICE_BEEP_CARD_W, DEVICE_BEEP_CARD_H,
                        30, 0x00ffffff, 0x00ff7a45);
    lcd_word_show_color(570, 168, "蜂鸣器", 0x00172033);
    ui_draw_status_text(604, 226, device_beep_state, 1);
    ui_draw_toggle(590, 286, device_beep_state, 0x00ff7a45);
}

static void ui_draw_screen(const ui_screen_def_t *screen)
{
    int i;

    if(screen == NULL)
        return;

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
        ui_draw_monitor_mock();
    else if(strcmp(screen->id, "device") == 0)
        ui_draw_device_mock();

    for(i = 0; i < screen->button_count; i++)
        ui_draw_button(&screen->buttons[i]);
}

static void ui_draw_album_overlay(int index, int count)
{
    char status[64];

    ui_draw_button_rect(ALBUM_BACK_X, ALBUM_BACK_Y,
                        ALBUM_BACK_W, ALBUM_BACK_H,
                        "返回", 0x00ffffff, 0x00172033);

    ui_draw_glass_panel(332, 18, 138, 48, 24, 0x00ffffff, 0x002874ff);

    snprintf(status, sizeof(status), "%d / %d", index + 1, count);
    lcd_word_show_color(362, 28, status, 0x00172033);
}

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

static void ui_device_cleanup(void)
{
    int i;

    for(i = 0; i < device_led_count && i < DEVICE_LED_COUNT; i++)
        led_set(i, 0);

    led_uninit();
    pwm_beep_stop();
    beep_ctl(0);
}

static int ui_device_led_toggle_hit(int x, int y, int *led_index)
{
    int i;

    for(i = 0; i < DEVICE_LED_COUNT; i++) {
        int toggle_x = 366;
        int toggle_y = 202 + i * 38;

        if(ui_point_in_rect(x, y, toggle_x, toggle_y,
                            DEVICE_TOGGLE_W, DEVICE_TOGGLE_H)) {
            if(led_index != NULL)
                *led_index = i;
            return 1;
        }
    }

    return 0;
}

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

        if(beep_ctl(next_state ? 1 : 0) == 0)
            device_beep_state = next_state;
        else
            LOG_ERROR("device panel set beep failed");

        return 1;
    }

    return 0;
}

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

int main(void)
{
    const ui_screen_def_t *screen = ui_find_screen(ui_app_initial_screen);
    int ret = 0;

    if(screen == NULL) {
        LOG_ERROR("initial screen not found: %s", ui_app_initial_screen);
        return 1;
    }

    if(lcd_init() != 0)
        return 1;

    if(lcd_word_init() != 0) {
        lcd_uninit();
        return 1;
    }

    if(touch_init() != 0) {
        lcd_word_uninit();
        lcd_uninit();
        return 1;
    }

    ui_device_init();
    ui_draw_screen(screen);

    while(1) {
        touch_event_t event;
        const ui_screen_def_t *next_screen;

        if(touch_read(&event) != 0) {
            ret = 1;
            break;
        }

        next_screen = ui_handle_touch(screen, &event);
        if(next_screen != screen) {
            screen = next_screen;
            ui_draw_screen(screen);
        }
    }

    touch_uninit();
    ui_device_cleanup();
    lcd_word_uninit();
    lcd_uninit();

    return ret;
}
