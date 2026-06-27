#include "GEC6818.h"

/**
 * @file pic.c
 * @brief 图片列表、图片页面缓存和滑动绘制实现。
 *
 * @details 功能：
 * 1. 扫描 PIC_DIR 目录下的图片文件，并按文件名排序。
 * 2. 将图片等比缩放到 LCD 屏幕内最大尺寸，居中显示，空白区域填黑。
 * 3. 使用触摸屏左右滑动切换图片，并在滑动过程中跟手显示动画。
 * 4. 为避免滑动时掉帧，只缓存当前图片、上一张图片、下一张图片，
 *    不会把目录里的所有图片一次性放进内存。
 *
 * @details 显示策略：
 * - pic_cache_t 保存已经缩放到 800x480 页面里的图片像素。
 * - frame_cache_t 是离屏帧缓存，滑动时先把两张页面拼到这里，
 *   再一次性拷贝到 framebuffer，减少屏幕刷新过程中的闪烁和撕裂感。
 */

/** @brief 松手后的补间动画帧数。帧数越大越平滑，但 CPU 占用和耗时也越高。 */
#define SLIDE_ANIMATION_FRAMES 10

/** @brief 补间动画每帧间隔，16000us 约等于 60 FPS 的单帧时间。 */
#define SLIDE_ANIMATION_DELAY_US 16000

/** @brief 屏幕上的可见矩形区域，用于裁剪超出 LCD 边界的图片。 */
typedef struct {
    int x;
    int y;
    int length;
    int width;
} rect_t;

/**
 * @brief 判断字符串是否以指定后缀结尾，大小写不敏感。
 *
 * @param[in] text 待检查的字符串。
 * @param[in] suffix 后缀字符串，例如 ".png"。
 *
 * @retval 1 text 以后缀 suffix 结尾。
 * @retval 0 不匹配。
 */
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

/**
 * @brief 判断文件名是否是当前图片解码库支持的格式。
 *
 * @param[in] name 目录项文件名。
 *
 * @retval 1 支持的图片后缀。
 * @retval 0 其他文件。
 */
static int is_supported_image(const char *name)
{
    return string_ends_with_ignore_case(name, ".png") ||
           string_ends_with_ignore_case(name, ".jpg") ||
           string_ends_with_ignore_case(name, ".jpeg") ||
           string_ends_with_ignore_case(name, ".bmp");
}

/**
 * @brief qsort 使用的字符串比较函数。
 *
 * @param[in] left 指向 char * 的二级指针。
 * @param[in] right 指向 char * 的二级指针。
 *
 * @retval <0 left 排在 right 前。
 * @retval 0 两者相等。
 * @retval >0 left 排在 right 后。
 */
static int image_path_compare(const void *left, const void *right)
{
    const char *left_path = *(const char * const *)left;
    const char *right_path = *(const char * const *)right;

    return strcmp(left_path, right_path);
}

/**
 * @brief 释放图片路径列表。
 *
 * @param[in,out] list 要释放的图片列表。
 *
 *
 * 函数会释放 paths 数组以及数组内每个字符串，并把结构体清零，
 * 因此可以安全重复调用。
 */
void image_list_free(image_list_t *list)
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

/**
 * @brief 向图片列表追加一个路径。
 *
 * @param[in,out] list 目标图片列表。
 * @param[in] dir 图片目录。
 * @param[in] name 图片文件名。
 *
 * @retval 0 追加成功。
 * @retval -1 内存分配失败。
 *
 *
 * 该函数会把 dir/name 拼成完整路径并复制到堆内存中，调用者最终
 * 需要通过 image_list_free() 释放。
 */
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

/**
 * @brief 扫描目录并加载图片文件列表。
 *
 * @param[out] list 输出图片列表。
 * @param[in] dir 要扫描的图片目录。
 *
 * @retval 0 扫描成功，list 中按文件名排序保存所有支持的图片路径。
 * @retval -1 打开目录失败。
 * @retval -2 添加路径时内存分配失败。
 */
int load_image_list(image_list_t *list, const char *dir)
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

/**
 * @brief 将 8 位 R/G/B 分量打包成 framebuffer 使用的 0x00RRGGBB 格式。
 *
 * @param[in] r 红色分量。
 * @param[in] g 绿色分量。
 * @param[in] b 蓝色分量。
 *
 * @return 0x00RRGGBB 格式的像素值。
 */
static unsigned int rgb888(unsigned char r, unsigned char g, unsigned char b)
{
    return ((unsigned int)r << 16) |
           ((unsigned int)g << 8) |
           (unsigned int)b;
}

/**
 * @brief 将 RGBA 像素预混合到黑色背景上。
 *
 * @param[in] r 图片解码出的红色分量。
 * @param[in] g 图片解码出的绿色分量。
 * @param[in] b 图片解码出的蓝色分量。
 * @param[in] a 图片解码出的 alpha 分量。
 *
 * @return 可直接写入 LCD 的 0x00RRGGBB 像素。
 *
 *
 * 图片页面缓存本身不保留 alpha 通道。透明区域预先与黑色背景混合，
 * 这样滑动时只需要拷贝像素，不需要每帧做 alpha 混合。
 */
static unsigned int blend_with_black(unsigned char r,
                                     unsigned char g,
                                     unsigned char b,
                                     unsigned char a)
{
    if(a == 255)
        return rgb888(r, g, b);

    return rgb888(r * a / 255, g * a / 255, b * a / 255);
}

/**
 * @brief 释放单张图片页面缓存。
 *
 * @param[in,out] cache 要释放的图片缓存。
 *
 *
 * 函数会释放 pixels，并把尺寸和指针清零。结构体清零后可以重新加载。
 */
void pic_cache_free(pic_cache_t *cache)
{
    if(cache == NULL)
        return;

    free(cache->pixels);
    cache->pixels = NULL;
    cache->length = 0;
    cache->width = 0;
}

/**
 * @brief 加载图片并生成 800x480 页面缓存。
 *
 * @param[out] cache 输出缓存。调用前应为空，或由 load_cache_at() 先释放旧内容。
 * @param[in] pic_path 图片文件路径。
 *
 * @retval 0 加载成功。
 * @retval -1 参数无效。
 * @retval -2 图片解码失败。
 * @retval -3 图片尺寸异常。
 * @retval -4 页面缓存内存分配失败。
 *
 * @details 显示规则：
 * 图片会按原始比例等比缩放到屏幕内的最大尺寸，并居中显示。
 * 例如竖图会上下撑满并左右留黑边，宽图会左右撑满并上下留黑边。
 */
int pic_cache_load(pic_cache_t *cache, const char *pic_path)
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

    /** @brief 比较 LCD_W/pic.width 与 LCD_H/pic.height，选择不会超屏的缩放边。 */
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

    /**
     * @brief 最近邻缩放。
     *
     * @details 性能开销低，适合当前 ARM 板实时交互场景。
     * 如果以后追求更高画质，可以在这里替换成双线性插值。
     */
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

/**
 * @brief 按图片索引加载页面缓存。
 *
 * @param[in] list 已排序的图片路径列表。
 * @param[in] index 要加载的图片索引。
 * @param[in,out] cache 输出缓存。函数会先释放 cache 内旧像素。
 *
 * @retval 0 加载成功。
 * @retval -1 参数或索引无效。
 * @retval <0 pic_cache_load() 返回的错误。
 */
int load_cache_at(const image_list_t *list, int index, pic_cache_t *cache)
{
    if(list == NULL || cache == NULL || index < 0 || index >= list->count)
        return -1;

    LOG_INFO("load image %d/%d: %s", index + 1, list->count, list->paths[index]);

    pic_cache_free(cache);
    return pic_cache_load(cache, list->paths[index]);
}

/**
 * @brief 初始化离屏帧缓存。
 *
 * @param[out] frame 输出帧缓存。
 *
 * @retval 0 初始化成功。
 * @retval -1 参数无效。
 * @retval -2 内存分配失败。
 */
int frame_cache_init(frame_cache_t *frame)
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
void frame_cache_free(frame_cache_t *frame)
{
    if(frame == NULL)
        return;

    free(frame->pixels);
    frame->pixels = NULL;
}

/**
 * @brief 计算一个矩形落在 LCD 屏幕内的可见部分。
 *
 * @param[in] x 原始矩形左上角横坐标。
 * @param[in] y 原始矩形左上角纵坐标。
 * @param[in] length 原始矩形宽度。
 * @param[in] width 原始矩形高度。
 * @param[out] rect 输出的屏幕内可见矩形。
 *
 * @retval 1 有可见区域。
 * @retval 0 完全不可见，或参数无效。
 */
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

/**
 * @brief 将图片页面缓存画到离屏帧缓存。
 *
 * @param[in,out] frame 目标离屏帧缓存。
 * @param[in] cache 源图片页面缓存。
 * @param[in] img_x 图片页面左上角在屏幕坐标系中的横坐标。
 * @param[in] img_y 图片页面左上角在屏幕坐标系中的纵坐标。
 *
 *
 * 图片可能处于滑动过程中的屏幕外位置，所以这里会先裁剪可见区域，
 * 然后按行 memcpy，避免逐像素写入带来的额外开销。
 */
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

/**
 * @brief 将离屏帧缓存刷新到 LCD framebuffer。
 *
 * @param[in] frame 已经绘制好的离屏帧缓存。
 *
 *
 * lcd_get_p() 返回 mmap 后的 framebuffer 地址。这里一次性 memcpy 整屏，
 * 可以减少边画边显示导致的中间状态。
 */
static void frame_cache_flush(const frame_cache_t *frame)
{
    unsigned int (*fb)[LCD_W] = lcd_get_p();

    if(frame == NULL || frame->pixels == NULL || fb == NULL)
        return;

    memcpy(fb, frame->pixels, LCD_SIZE);
}

/**
 * @brief 显示单张已缓存图片。
 *
 * @param[in,out] frame 离屏帧缓存。
 * @param[in] cache 要显示的图片页面缓存。
 *
 * @retval 0 显示成功。
 * @retval -1 参数无效。
 */
int show_cached_image(frame_cache_t *frame, const pic_cache_t *cache)
{
    if(frame == NULL || cache == NULL || cache->pixels == NULL)
        return -1;

    frame_cache_draw_pic(frame, cache, 0, 0);
    frame_cache_flush(frame);

    return 0;
}

/**
 * @brief 计算下一张图片索引，支持从最后一张循环回第一张。
 *
 * @param[in] index 当前索引。
 * @param[in] count 图片总数。
 *
 * @return 下一张图片索引。
 */
int next_index(int index, int count)
{
    return (index + 1) % count;
}

/**
 * @brief 计算上一张图片索引，支持从第一张循环到最后一张。
 *
 * @param[in] index 当前索引。
 * @param[in] count 图片总数。
 *
 * @return 上一张图片索引。
 */
int prev_index(int index, int count)
{
    return (index + count - 1) % count;
}

/**
 * @brief 加载当前图片两侧的邻居缓存。
 *
 * @param[in] list 图片路径列表。
 * @param[in] current_index 当前显示的图片索引。
 * @param[out] prev_pic 输出上一张图片缓存。
 * @param[out] next_pic 输出下一张图片缓存。
 *
 * @retval 0 加载成功，或图片数量不超过 1 时无需加载。
 * @retval -1 上一张加载失败。
 * @retval -2 下一张加载失败。
 *
 *
 * 程序只持有当前、上一张、下一张三份图片页面缓存。
 * 这样滑动跟手时不会临时解码图片，同时内存占用不会随图片总数增长。
 */
int load_neighbor_caches(const image_list_t *list,
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

/**
 * @brief 限制滑动偏移量不超过一屏宽。
 *
 * @param[in] offset 手指移动产生的横向偏移。
 *
 * @return 被限制在 [-LCD_W, LCD_W] 范围内的偏移。
 */
int clamp_slide_offset(int offset)
{
    if(offset < -LCD_W)
        return -LCD_W;

    if(offset > LCD_W)
        return LCD_W;

    return offset;
}

/**
 * @brief 绘制滑动过程中的一帧。
 *
 * @param[in,out] frame 离屏帧缓存。
 * @param[in] current_pic 当前图片页面缓存。
 * @param[in] target_pic 目标图片页面缓存。
 * @param[in] offset 当前图片相对原位置的横向偏移。
 * @param[in] target_is_next 1 表示目标图是下一张，0 表示目标图是上一张。
 *
 * @retval 0 绘制成功。
 * @retval -1 参数无效或缓存未加载。
 *
 *
 * 当前图跟随手指移动；目标图从屏幕左侧或右侧进入。
 */
int draw_slide_frame(frame_cache_t *frame,
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

/**
 * @brief 松手后补齐滑动动画。
 *
 * @param[in,out] frame 离屏帧缓存。
 * @param[in] current_pic 当前图片页面缓存。
 * @param[in] target_pic 目标图片页面缓存。
 * @param[in] from_offset 松手时当前图片的横向偏移。
 * @param[in] to_offset 动画结束时当前图片的横向偏移。
 * @param[in] target_is_next 目标图方向，1 为下一张，0 为上一张。
 *
 * @retval 0 动画绘制完成。
 *
 *
 * 使用 smoothstep 风格的缓入缓出曲线，让回弹或完成切换看起来更自然。
 */
int animate_slide(frame_cache_t *frame,
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
