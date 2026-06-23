#include "GEC6818.h"

#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>

#define PIC_DIR "./pic"
#define SWIPE_MIN_DISTANCE 80
#define SLIDE_ANIMATION_FRAMES 10
#define SLIDE_ANIMATION_DELAY_US 16000

typedef struct {
    char **paths;
    int count;
    int capacity;
} image_list_t;

typedef struct {
    int x;
    int y;
    int length;
    int width;
} rect_t;

typedef struct {
    int length;
    int width;
    unsigned int *pixels;
} pic_cache_t;

typedef struct {
    unsigned int *pixels;
} frame_cache_t;

static int string_ends_with_ignore_case(const char *text, const char *suffix)
{
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);

    if(text_len < suffix_len)
        return 0;

    text += text_len - suffix_len;

    while(*suffix != '\0') {
        if(tolower((unsigned char)*text) != tolower((unsigned char)*suffix))
            return 0;

        text++;
        suffix++;
    }

    return 1;
}

static int is_supported_image(const char *name)
{
    return string_ends_with_ignore_case(name, ".png") ||
           string_ends_with_ignore_case(name, ".jpg") ||
           string_ends_with_ignore_case(name, ".jpeg") ||
           string_ends_with_ignore_case(name, ".bmp");
}

static int image_path_compare(const void *left, const void *right)
{
    const char *left_path = *(const char * const *)left;
    const char *right_path = *(const char * const *)right;

    return strcmp(left_path, right_path);
}

static void image_list_free(image_list_t *list)
{
    if(list == NULL)
        return;

    int i;

    for(i = 0; i < list->count; i++)
        free(list->paths[i]);

    free(list->paths);
    list->paths = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int image_list_push(image_list_t *list, const char *dir, const char *name)
{
    if(list->count == list->capacity) {
        int new_capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        char **new_paths = realloc(list->paths,
                                   (size_t)new_capacity * sizeof(list->paths[0]));

        if(new_paths == NULL)
            return -1;

        list->paths = new_paths;
        list->capacity = new_capacity;
    }

    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    char *path = malloc(dir_len + 1 + name_len + 1);

    if(path == NULL)
        return -1;

    snprintf(path, dir_len + 1 + name_len + 1, "%s/%s", dir, name);
    list->paths[list->count] = path;
    list->count++;

    return 0;
}

static int load_image_list(image_list_t *list, const char *dir)
{
    DIR *dp = opendir(dir);

    if(dp == NULL) {
        LOG_PERROR("opendir pic");
        return -1;
    }

    while(1) {
        struct dirent *entry = readdir(dp);

        if(entry == NULL)
            break;

        if(!is_supported_image(entry->d_name))
            continue;

        if(image_list_push(list, dir, entry->d_name) != 0) {
            closedir(dp);
            image_list_free(list);
            return -2;
        }
    }

    closedir(dp);

    if(list->count > 1)
        qsort(list->paths, (size_t)list->count, sizeof(list->paths[0]),
              image_path_compare);

    return 0;
}

static unsigned int rgb888(unsigned char r, unsigned char g, unsigned char b)
{
    return ((unsigned int)r << 16) |
           ((unsigned int)g << 8) |
           (unsigned int)b;
}

static unsigned int blend_with_black(unsigned char r,
                                     unsigned char g,
                                     unsigned char b,
                                     unsigned char a)
{
    if(a == 255)
        return rgb888(r, g, b);

    return rgb888(r * a / 255, g * a / 255, b * a / 255);
}

static void pic_cache_free(pic_cache_t *cache)
{
    if(cache == NULL)
        return;

    free(cache->pixels);
    cache->pixels = NULL;
    cache->length = 0;
    cache->width = 0;
}

static int pic_cache_load(pic_cache_t *cache, const char *pic_path)
{
    lcd_pic_t pic = {0, 0, NULL};
    int fit_w;
    int fit_h;
    int offset_x;
    int offset_y;
    int x;
    int y;

    if(cache == NULL || pic_path == NULL)
        return -1;

    if(lcd_pic_load(&pic, pic_path) != 0)
        return -2;

    if(pic.width <= 0 || pic.height <= 0) {
        lcd_pic_free(&pic);
        return -3;
    }

    cache->pixels = malloc((size_t)LCD_W * LCD_H * sizeof(cache->pixels[0]));
    if(cache->pixels == NULL) {
        lcd_pic_free(&pic);
        return -4;
    }

    cache->length = LCD_W;
    cache->width = LCD_H;
    memset(cache->pixels, 0, (size_t)LCD_W * LCD_H * sizeof(cache->pixels[0]));

    if((long)LCD_W * pic.height <= (long)LCD_H * pic.width) {
        fit_w = LCD_W;
        fit_h = (int)((long)LCD_W * pic.height / pic.width);
    } else {
        fit_h = LCD_H;
        fit_w = (int)((long)LCD_H * pic.width / pic.height);
    }

    if(fit_w <= 0)
        fit_w = 1;

    if(fit_h <= 0)
        fit_h = 1;

    offset_x = (LCD_W - fit_w) / 2;
    offset_y = (LCD_H - fit_h) / 2;

    for(y = 0; y < fit_h; y++) {
        int src_y = y * pic.height / fit_h;

        for(x = 0; x < fit_w; x++) {
            int src_x = x * pic.width / fit_w;
            const unsigned char *px = pic.rgba + (src_y * pic.width + src_x) * 4;
            int dst_x = offset_x + x;
            int dst_y = offset_y + y;

            cache->pixels[dst_y * LCD_W + dst_x] =
                blend_with_black(px[0], px[1], px[2], px[3]);
        }
    }

    lcd_pic_free(&pic);

    return 0;
}

static int load_cache_at(const image_list_t *list, int index, pic_cache_t *cache)
{
    if(list == NULL || cache == NULL || index < 0 || index >= list->count)
        return -1;

    LOG_INFO("load image %d/%d: %s", index + 1, list->count, list->paths[index]);

    pic_cache_free(cache);
    return pic_cache_load(cache, list->paths[index]);
}

static int frame_cache_init(frame_cache_t *frame)
{
    if(frame == NULL)
        return -1;

    frame->pixels = malloc(LCD_SIZE);
    if(frame->pixels == NULL)
        return -2;

    return 0;
}

static void frame_cache_free(frame_cache_t *frame)
{
    if(frame == NULL)
        return;

    free(frame->pixels);
    frame->pixels = NULL;
}

static int visible_rect(int x, int y, int length, int width, rect_t *rect)
{
    long right = (long)x + length;
    long bottom = (long)y + width;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = right > LCD_W ? LCD_W : (int)right;
    int y1 = bottom > LCD_H ? LCD_H : (int)bottom;

    if(rect == NULL)
        return 0;

    if(x0 >= x1 || y0 >= y1)
        return 0;

    rect->x = x0;
    rect->y = y0;
    rect->length = x1 - x0;
    rect->width = y1 - y0;

    return 1;
}

static void frame_cache_draw_pic(frame_cache_t *frame,
                                 const pic_cache_t *cache,
                                 int img_x,
                                 int img_y)
{
    rect_t rect = {0, 0, 0, 0};
    int y;

    if(frame == NULL || frame->pixels == NULL ||
       cache == NULL || cache->pixels == NULL)
        return;

    if(!visible_rect(img_x, img_y, cache->length, cache->width, &rect))
        return;

    for(y = 0; y < rect.width; y++) {
        int dst_y = rect.y + y;
        int src_y = dst_y - img_y;
        int src_x = rect.x - img_x;

        memcpy(&frame->pixels[dst_y * LCD_W + rect.x],
               &cache->pixels[src_y * cache->length + src_x],
               (size_t)rect.length * sizeof(frame->pixels[0]));
    }
}

static void frame_cache_flush(const frame_cache_t *frame)
{
    unsigned int (*fb)[LCD_W] = lcd_get_p();

    if(frame == NULL || frame->pixels == NULL || fb == NULL)
        return;

    memcpy(fb, frame->pixels, LCD_SIZE);
}

static int show_cached_image(frame_cache_t *frame, const pic_cache_t *cache)
{
    if(frame == NULL || cache == NULL || cache->pixels == NULL)
        return -1;

    frame_cache_draw_pic(frame, cache, 0, 0);
    frame_cache_flush(frame);

    return 0;
}

static int next_index(int index, int count)
{
    return (index + 1) % count;
}

static int prev_index(int index, int count)
{
    return (index + count - 1) % count;
}

static int load_neighbor_caches(const image_list_t *list,
                                int current_index,
                                pic_cache_t *prev_pic,
                                pic_cache_t *next_pic)
{
    if(list == NULL || list->count <= 1)
        return 0;

    if(load_cache_at(list, prev_index(current_index, list->count), prev_pic) != 0)
        return -1;

    if(load_cache_at(list, next_index(current_index, list->count), next_pic) != 0)
        return -2;

    return 0;
}

static int clamp_slide_offset(int offset)
{
    if(offset < -LCD_W)
        return -LCD_W;

    if(offset > LCD_W)
        return LCD_W;

    return offset;
}

static int draw_slide_frame(frame_cache_t *frame,
                            const pic_cache_t *current_pic,
                            const pic_cache_t *target_pic,
                            int offset,
                            int target_is_next)
{
    int target_x;

    if(frame == NULL || current_pic == NULL || current_pic->pixels == NULL ||
       target_pic == NULL || target_pic->pixels == NULL)
        return -1;

    offset = clamp_slide_offset(offset);
    target_x = target_is_next ? offset + LCD_W : offset - LCD_W;

    frame_cache_draw_pic(frame, current_pic, offset, 0);
    frame_cache_draw_pic(frame, target_pic, target_x, 0);
    frame_cache_flush(frame);

    return 0;
}

static int animate_slide(frame_cache_t *frame,
                         const pic_cache_t *current_pic,
                         const pic_cache_t *target_pic,
                         int from_offset,
                         int to_offset,
                         int target_is_next)
{
    int i;

    for(i = 1; i <= SLIDE_ANIMATION_FRAMES; i++) {
        long progress = (long)3 * SLIDE_ANIMATION_FRAMES * i * i -
                        (long)2 * i * i * i;
        long denom = (long)SLIDE_ANIMATION_FRAMES *
                     SLIDE_ANIMATION_FRAMES *
                     SLIDE_ANIMATION_FRAMES;
        int offset = from_offset +
                     (int)((long)(to_offset - from_offset) * progress / denom);

        draw_slide_frame(frame, current_pic, target_pic, offset, target_is_next);
        usleep(SLIDE_ANIMATION_DELAY_US);
    }

    return 0;
}

int main(void)
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
    int ret = 0;

    if(load_image_list(&images, PIC_DIR) != 0)
        return 1;

    if(images.count == 0) {
        LOG_ERROR("no image found in %s", PIC_DIR);
        image_list_free(&images);
        return 1;
    }

    if(lcd_init() != 0) {
        image_list_free(&images);
        return 1;
    }

    if(touch_init() != 0) {
        lcd_uninit();
        image_list_free(&images);
        return 1;
    }

    if(frame_cache_init(&frame) != 0) {
        ret = 1;
        goto cleanup;
    }

    if(load_cache_at(&images, current_index, &current_pic) != 0 ||
       show_cached_image(&frame, &current_pic) != 0) {
        ret = 1;
        goto cleanup;
    }

    if(load_neighbor_caches(&images, current_index, &prev_pic, &next_pic) != 0) {
        ret = 1;
        goto cleanup;
    }

    while(1) {
        touch_event_t event;

        if(touch_read(&event) != 0) {
            ret = 1;
            break;
        }

        if(event.type == TOUCH_EVENT_DOWN) {
            down_x = event.x;
            down_y = event.y;
            has_down = 1;
        } else if(event.type == TOUCH_EVENT_UP && has_down) {
            int dx = event.x - down_x;
            int dy = event.y - down_y;
            int completed = abs(dx) >= SWIPE_MIN_DISTANCE && abs(dx) > abs(dy);
            int final_offset = 0;
            int target_is_next = 0;
            int target_index = current_index;
            pic_cache_t *target_pic = NULL;

            has_down = 0;

            if(images.count <= 1 || abs(dx) <= abs(dy)) {
                show_cached_image(&frame, &current_pic);
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

                    if(load_cache_at(&images,
                                     next_index(current_index, images.count),
                                     &next_pic) != 0) {
                        ret = 1;
                        break;
                    }
                } else {
                    pic_cache_free(&next_pic);
                    next_pic = current_pic;
                    current_pic = prev_pic;
                    prev_pic = (pic_cache_t){0, 0, NULL};
                    current_index = target_index;
                    show_cached_image(&frame, &current_pic);

                    if(load_cache_at(&images,
                                     prev_index(current_index, images.count),
                                     &prev_pic) != 0) {
                        ret = 1;
                        break;
                    }
                }
            } else {
                show_cached_image(&frame, &current_pic);
            }

        } else if(event.type == TOUCH_EVENT_MOVE && has_down) {
            int dx = event.x - down_x;
            int dy = event.y - down_y;
            int target_is_next;
            pic_cache_t *target_pic;

            if(images.count <= 1 || abs(dx) <= abs(dy)) {
                continue;
            }

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
    touch_uninit();
    lcd_uninit();
    image_list_free(&images);

    return ret;
}
