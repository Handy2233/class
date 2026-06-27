#ifndef __PIC_H__
#define __PIC_H__

/**
 * @file pic.h
 * @brief 图片扫描、图片缓存和滑动绘制接口。
 *
 * @details 该模块只封装图片相关能力，不包含触摸事件循环和程序主流程：
 * 1. 扫描指定目录，收集 PNG/JPEG/BMP 图片路径。
 * 2. 将单张图片等比缩放并居中到 LCD_W x LCD_H 页面缓存。
 * 3. 管理一块和 framebuffer 同尺寸的离屏帧缓存。
 * 4. 根据横向偏移绘制当前图片和目标图片，实现左右滑动动画。
 *
 * @details 内存所有权约定：
 * - image_list_t 由 load_image_list() 分配路径字符串，使用后必须调用
 *   image_list_free()。
 * - pic_cache_t 由 pic_cache_load() 或 load_cache_at() 分配像素缓存，
 *   使用后必须调用 pic_cache_free()。
 * - frame_cache_t 由 frame_cache_init() 分配整屏离屏缓存，使用后必须调用
 *   frame_cache_free()。
 *
 * @details 坐标和尺寸约定：
 * - 本模块使用 LCD_W/LCD_H 作为页面尺寸，和 lcd.h 中 framebuffer 尺寸保持一致。
 * - 滑动偏移 offset 为当前图片相对屏幕左上角的横向偏移，左滑为负，右滑为正。
 * - target_is_next 为 1 时目标图片从右侧进入，为 0 时目标图片从左侧进入。
 */

/**
 * @brief 动态图片路径列表。
 *
 * @param paths 路径数组，paths[i] 指向 malloc 分配的完整图片路径。
 * @param count 当前已经保存的图片路径数量。
 * @param capacity paths 数组当前可容纳的元素数量。
 *
 * @details 调用者不应直接释放 paths 或 paths[i]，应统一调用
 * image_list_free()，避免漏释放或重复释放。
 */
typedef struct {
    char **paths;
    int count;
    int capacity;
} image_list_t;

/**
 * @brief 单张图片的页面缓存。
 *
 * @param length 缓存横向宽度，目前固定为 LCD_W。
 * @param width 缓存纵向高度，目前固定为 LCD_H。
 * @param pixels 缩放并居中后的 0x00RRGGBB 像素数据。
 *
 * @details pixels 的布局为逐行连续存储，索引方式为
 * pixels[y * length + x]。缓存格式和 framebuffer 一致，所以滑动时
 * 可以直接按行 memcpy 到离屏帧缓存或 framebuffer。
 */
typedef struct {
    int length;
    int width;
    unsigned int *pixels;
} pic_cache_t;

/**
 * @brief 离屏帧缓存。
 *
 * @param pixels LCD_SIZE 字节的整屏像素缓存，格式为 0x00RRGGBB。
 *
 * @details 每次滑动绘制时，先把当前图和目标图按偏移量画入
 * frame_cache_t，再整体刷新到 LCD framebuffer。这样可以避免边画
 * 边显示时出现撕裂或中间状态。
 */
typedef struct {
    unsigned int *pixels;
} frame_cache_t;

/**
 * @brief 扫描目录并加载图片文件列表。
 *
 * @param[out] list 输出图片列表。
 * @param[in] dir 要扫描的图片目录，例如 "./pic"。
 *
 * @retval 0 扫描成功。即使目录中没有图片，也会返回 0，此时 count 为 0。
 * @retval -1 打开目录失败，通常是目录不存在或权限不足。
 * @retval -2 添加路径时内存分配失败。
 *
 * @details 函数只收集当前图片库支持的后缀：png、jpg、jpeg、bmp。
 * 扫描完成后会按完整路径字符串排序，保证浏览顺序稳定。
 */
int load_image_list(image_list_t *list, const char *dir);

/**
 * @brief 释放图片路径列表。
 *
 * @param[in,out] list 要释放的图片列表。
 *
 * @details 函数会释放 paths 数组和数组内每个路径字符串，并把
 * paths/count/capacity 清零。list 为 NULL 时函数直接返回。
 */
void image_list_free(image_list_t *list);

/**
 * @brief 加载图片并生成 800x480 页面缓存。
 *
 * @param[out] cache 输出缓存。调用前应为空，或先用 pic_cache_free() 清理旧内容。
 * @param[in] pic_path 图片文件路径。
 *
 * @retval 0 加载成功。
 * @retval -1 参数无效。
 * @retval -2 图片解码失败。
 * @retval -3 图片尺寸异常。
 * @retval -4 页面缓存内存分配失败。
 *
 * @details 图片会按原始比例等比缩放到屏幕内最大尺寸，并居中放入
 * LCD_W x LCD_H 页面。透明像素会预先和黑色背景混合，使滑动绘制时
 * 不需要逐帧进行 alpha 混合。
 */
int pic_cache_load(pic_cache_t *cache, const char *pic_path);

/**
 * @brief 释放单张图片页面缓存。
 *
 * @param[in,out] cache 要释放的图片缓存。
 *
 * @details 函数会释放 pixels，并把 length/width/pixels 清零。
 * cache 为 NULL 时函数直接返回，可安全重复调用。
 */
void pic_cache_free(pic_cache_t *cache);

/**
 * @brief 按图片索引加载页面缓存。
 *
 * @param[in] list 已排序的图片路径列表。
 * @param[in] index 要加载的图片索引。
 * @param[in,out] cache 输出缓存。函数会先释放 cache 内旧像素。
 *
 * @retval 0 加载成功。
 * @retval -1 参数无效或 index 越界。
 * @retval <0 pic_cache_load() 返回的图片加载错误。
 *
 * @details 该函数是 pic_cache_load() 的索引封装，适合图片浏览器按
 * 当前下标加载当前图、上一张图、下一张图。
 */
int load_cache_at(const image_list_t *list, int index, pic_cache_t *cache);

/**
 * @brief 初始化离屏帧缓存。
 *
 * @param[out] frame 输出帧缓存。
 *
 * @retval 0 初始化成功。
 * @retval -1 参数无效。
 * @retval -2 内存分配失败。
 *
 * @details frame->pixels 会分配 LCD_SIZE 字节，尺寸和 LCD framebuffer
 * 完全一致。调用成功后必须使用 frame_cache_free() 释放。
 */
int frame_cache_init(frame_cache_t *frame);

/**
 * @brief 释放离屏帧缓存。
 *
 * @param[in,out] frame 要释放的帧缓存。
 *
 * @details 函数会释放 frame->pixels 并置空。frame 为 NULL 时直接返回。
 */
void frame_cache_free(frame_cache_t *frame);

/**
 * @brief 显示单张已缓存图片。
 *
 * @param[in,out] frame 离屏帧缓存。
 * @param[in] cache 要显示的图片页面缓存。
 *
 * @retval 0 显示成功。
 * @retval -1 参数无效。
 *
 * @details 函数会把 cache 绘制到离屏帧缓存左上角，再把整块离屏缓存
 * 刷新到 LCD framebuffer。调用前必须确保 lcd_init() 已成功。
 */
int show_cached_image(frame_cache_t *frame, const pic_cache_t *cache);

/**
 * @brief 计算下一张图片索引，支持循环。
 *
 * @param[in] index 当前索引。
 * @param[in] count 图片总数。
 *
 * @return 下一张图片索引。
 *
 * @details index 为最后一张时返回 0，用于循环浏览。
 * 调用者应保证 count > 0。
 */
int next_index(int index, int count);

/**
 * @brief 计算上一张图片索引，支持循环。
 *
 * @param[in] index 当前索引。
 * @param[in] count 图片总数。
 *
 * @return 上一张图片索引。
 *
 * @details index 为 0 时返回 count - 1，用于循环浏览。
 * 调用者应保证 count > 0。
 */
int prev_index(int index, int count);

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
 * @details 程序只持有当前、上一张、下一张三份页面缓存。这样滑动跟手
 * 时不会临时解码图片，同时内存占用不会随图片总数增长。
 */
int load_neighbor_caches(const image_list_t *list,
                         int current_index,
                         pic_cache_t *prev_pic,
                         pic_cache_t *next_pic);

/**
 * @brief 限制滑动偏移量不超过一屏宽。
 *
 * @param[in] offset 手指移动产生的横向偏移。
 *
 * @return 被限制在 [-LCD_W, LCD_W] 范围内的偏移。
 *
 * @details 该函数防止一次 MOVE 事件产生超过一屏宽的绘制偏移，
 * 避免目标图片位置计算越过当前滑动动画的有效范围。
 */
int clamp_slide_offset(int offset);

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
 * @details 当前图跟随 offset 移动。target_is_next 为 1 时目标图从
 * 当前图右侧进入；为 0 时目标图从当前图左侧进入。函数内部会裁剪
 * 屏幕外区域，只拷贝可见像素。
 */
int draw_slide_frame(frame_cache_t *frame,
                     const pic_cache_t *current_pic,
                     const pic_cache_t *target_pic,
                     int offset,
                     int target_is_next);

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
 * @details 动画使用 smoothstep 风格的缓入缓出曲线，从 from_offset
 * 过渡到 to_offset。该函数只负责显示动画，不会改变当前图片索引或
 * 交换缓存所有权。
 */
int animate_slide(frame_cache_t *frame,
                  const pic_cache_t *current_pic,
                  const pic_cache_t *target_pic,
                  int from_offset,
                  int to_offset,
                  int target_is_next);

#endif
