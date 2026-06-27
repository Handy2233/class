#include "word.h"
#include "lcd.h"
#include "log.h"

#define STB_TRUETYPE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @file word.c
 * @brief UTF-8 中英文文字显示实现。
 *
 * @details 本文件直接面向 lcd.c 暴露的 framebuffer 地址绘制文字：
 * 1. 读取 font/word.json，得到字号、行距、颜色、中文字体和西文字体路径。
 * 2. 使用 stb_truetype 解析 TTF/TTC 字体文件，不依赖板子上额外安装 FreeType。
 * 3. 将 UTF-8 字符串逐个解码为 Unicode codepoint。
 * 4. 中文字符优先使用宋体，西文字符优先使用 Consolas；字体缺字时尝试回退。
 * 5. 将 stb_truetype 输出的灰度字形按 alpha 混合到 LCD framebuffer。
 *
 * @details 当前模块不会维护离屏文字缓存。每次调用 lcd_word_show() 都会
 * 重新栅格化并绘制字符串，适合少量 UI 文本、标题、状态提示等场景。
 */

/** @brief 默认文字配置文件路径。 */
#define WORD_DEFAULT_CONFIG "font/word.json"

/** @brief 默认中文字体路径。当前使用宋体 TTC。 */
#define WORD_DEFAULT_ZH_FONT "font/SimSun.ttc"

/** @brief 默认西文字体路径。当前使用 Consolas TTF。 */
#define WORD_DEFAULT_WEST_FONT "font/Consolas.ttf"

/** @brief JSON 配置中字体路径字符串的最大保存长度。 */
#define WORD_PATH_MAX 256

/**
 * @brief 已加载并由 stb_truetype 解析过的字体对象。
 *
 * @param data 完整字体文件内容，必须在 info 使用期间保持有效。
 * @param data_size 字体文件字节数，用于记录资源大小和调试。
 * @param info stb_truetype 初始化后的字体描述对象。
 * @param loaded 是否已成功加载并初始化字体。
 *
 * @details stbtt_fontinfo 不复制字体数据，只保存对 data 的引用。因此释放
 * data 前必须确保不再使用 info。
 */
typedef struct {
    unsigned char *data;
    long data_size;
    stbtt_fontinfo info;
    int loaded;
} word_font_t;

/**
 * @brief 文字模块运行配置。
 *
 * @param zh_font_path 中文字体路径。
 * @param west_font_path 西文字体路径。
 * @param zh_font_index 中文字体集合中的字体索引，普通 TTF 为 0。
 * @param west_font_index 西文字体集合中的字体索引，普通 TTF 为 0。
 * @param size 字号像素高度。
 * @param line_gap 多行文字之间的额外行距。
 * @param color 默认文字颜色，格式为 0x00RRGGBB。
 */
typedef struct {
    char zh_font_path[WORD_PATH_MAX];
    char west_font_path[WORD_PATH_MAX];
    int zh_font_index;
    int west_font_index;
    int size;
    int line_gap;
    unsigned int color;
} word_config_t;

/** @brief 当前加载的中文字体。 */
static word_font_t word_zh_font = {NULL, 0, {0}, 0};

/** @brief 当前加载的西文字体。 */
static word_font_t word_west_font = {NULL, 0, {0}, 0};

/** @brief 当前文字配置；模块未初始化时保存默认值。 */
static word_config_t word_config = {
    WORD_DEFAULT_ZH_FONT,
    WORD_DEFAULT_WEST_FONT,
    0,
    0,
    32,
    6,
    0x00ffffff
};

/** @brief 文字模块是否已经成功加载配置和两套字体。 */
static int word_ready = 0;

/**
 * @brief 将整个文件读取到内存。
 *
 * @param[in] path 文件路径。
 * @param[out] data 输出文件内容缓冲区。调用成功后由调用者 free()。
 * @param[out] size 输出文件字节数。
 *
 * @retval 0 读取成功。
 * @retval -1 参数无效。
 * @retval -2 打开文件失败。
 * @retval -3 定位到文件末尾失败。
 * @retval -4 文件长度异常。
 * @retval -5 分配缓冲区失败。
 * @retval -6 读取文件内容失败。
 *
 * @details 读取时会额外分配 1 字节并写入 '\0'，这样同一个函数既可用于
 * 二进制字体文件，也可用于把 JSON 当作 C 字符串解析。
 */
static int word_read_file(const char *path, unsigned char **data, long *size)
{
    FILE *fp;
    long file_size;
    unsigned char *buffer;

    if(path == NULL || data == NULL || size == NULL)
        return -1;

    fp = fopen(path, "rb");
    if(fp == NULL) {
        LOG_PERROR(path);
        return -2;
    }

    if(fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -3;
    }

    file_size = ftell(fp);
    if(file_size <= 0) {
        fclose(fp);
        return -4;
    }

    rewind(fp);

    buffer = malloc((size_t)file_size + 1);
    if(buffer == NULL) {
        fclose(fp);
        return -5;
    }

    if(fread(buffer, 1, (size_t)file_size, fp) != (size_t)file_size) {
        free(buffer);
        fclose(fp);
        return -6;
    }

    fclose(fp);
    buffer[file_size] = '\0';
    *data = buffer;
    *size = file_size;

    return 0;
}

/**
 * @brief 释放单个字体对象持有的文件缓冲区。
 *
 * @param[in,out] font 要释放的字体对象。
 *
 * @details 函数会清空 data/data_size/loaded。font 为 NULL 时直接返回。
 * stbtt_fontinfo 内部字段不需要单独释放，但 data 释放后不得再使用 info。
 */
static void word_font_free(word_font_t *font)
{
    if(font == NULL)
        return;

    free(font->data);
    font->data = NULL;
    font->data_size = 0;
    font->loaded = 0;
}

/**
 * @brief 加载并初始化一个 TTF/TTC 字体。
 *
 * @param[in,out] font 输出字体对象。加载成功前会释放 font 内旧资源。
 * @param[in] path 字体文件路径。
 * @param[in] font_index 字体集合索引。普通 TTF 通常为 0，TTC 可包含多个字体。
 *
 * @retval 0 加载成功。
 * @retval -1 参数无效。
 * @retval -2 字体文件读取失败。
 * @retval -3 stb_truetype 无法取得或初始化指定字体。
 */
static int word_font_load(word_font_t *font, const char *path, int font_index)
{
    unsigned char *data = NULL;
    long data_size = 0;
    int offset;

    if(font == NULL || path == NULL)
        return -1;

    if(word_read_file(path, &data, &data_size) != 0)
        return -2;

    offset = stbtt_GetFontOffsetForIndex(data, font_index);
    if(offset < 0 || stbtt_InitFont(&font->info, data, offset) == 0) {
        free(data);
        return -3;
    }

    word_font_free(font);
    font->data = data;
    font->data_size = data_size;
    font->loaded = 1;

    return 0;
}

/**
 * @brief 在简单 JSON 文本中查找指定 key 对应值的起始位置。
 *
 * @param[in] json JSON 文本。
 * @param[in] key 要查找的键名，不包含双引号。
 *
 * @retval 非NULL 指向冒号后第一个非空白字符。
 * @retval NULL 未找到 key 或格式不符合预期。
 *
 * @details 这里不是完整 JSON 解析器，只服务于 font/word.json 这种固定小配置。
 * 它支持任意空白，但不处理嵌套层级区分；因此配置文件中键名应保持唯一。
 */
static const char *word_json_find_value(const char *json, const char *key)
{
    char pattern[64];
    const char *pos;

    if(json == NULL || key == NULL)
        return NULL;

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    pos = strstr(json, pattern);
    if(pos == NULL)
        return NULL;

    pos += strlen(pattern);
    pos = strchr(pos, ':');
    if(pos == NULL)
        return NULL;

    pos++;
    while(*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n')
        pos++;

    return pos;
}

/**
 * @brief 从简单 JSON 文本中读取字符串值。
 *
 * @param[in] json JSON 文本。
 * @param[in] key 要读取的键名。
 * @param[out] out 输出字符串缓冲区。
 * @param[in] out_size 输出缓冲区大小。
 *
 * @retval 0 读取成功。
 * @retval -1 key 不存在或参数无效。
 * @retval -2 值不是字符串。
 * @retval -3 输出缓冲区不足。
 * @retval -4 字符串没有正确闭合。
 *
 * @details 支持最简单的反斜杠转义跳过，足够保存字体路径。
 */
static int word_json_get_string(const char *json,
                                const char *key,
                                char *out,
                                size_t out_size)
{
    const char *pos = word_json_find_value(json, key);
    size_t used = 0;

    if(pos == NULL || out == NULL || out_size == 0)
        return -1;

    if(*pos != '"')
        return -2;

    pos++;
    while(*pos != '\0' && *pos != '"') {
        char ch = *pos;

        if(ch == '\\' && pos[1] != '\0') {
            pos++;
            ch = *pos;
        }

        if(used + 1 >= out_size)
            return -3;

        out[used++] = ch;
        pos++;
    }

    if(*pos != '"')
        return -4;

    out[used] = '\0';
    return 0;
}

/**
 * @brief 从简单 JSON 文本中读取整数值。
 *
 * @param[in] json JSON 文本。
 * @param[in] key 要读取的键名。
 * @param[out] out 输出整数。
 *
 * @retval 0 读取成功。
 * @retval -1 key 不存在或参数无效。
 * @retval -2 值不能转换为整数。
 */
static int word_json_get_int(const char *json, const char *key, int *out)
{
    const char *pos = word_json_find_value(json, key);
    char *end = NULL;
    long value;

    if(pos == NULL || out == NULL)
        return -1;

    value = strtol(pos, &end, 10);
    if(end == pos)
        return -2;

    *out = (int)value;
    return 0;
}

/**
 * @brief 从简单 JSON 文本中读取颜色值。
 *
 * @param[in] json JSON 文本。
 * @param[in] key 要读取的键名。
 * @param[out] out 输出 0x00RRGGBB 格式颜色。
 *
 * @retval 0 读取成功。
 * @retval -1 out 参数无效。
 * @retval -2 字符串颜色值无法转换。
 * @retval -3 既不是可用字符串颜色，也不是可用整数颜色。
 *
 * @details 支持 "#RRGGBB"、"0xffffff" 这种字符串，也支持整数值。
 * 结果只保留低 24 位，和 LCD framebuffer 的颜色格式保持一致。
 */
static int word_json_get_color(const char *json, const char *key, unsigned int *out)
{
    char text[32];
    char *end = NULL;
    const char *start;
    unsigned long value;
    int int_value;

    if(out == NULL)
        return -1;

    if(word_json_get_string(json, key, text, sizeof(text)) == 0) {
        start = text;
        if(start[0] == '#')
            start++;

        value = strtoul(start, &end, 0);
        if(end == start)
            return -2;

        *out = (unsigned int)(value & 0x00ffffff);
        return 0;
    }

    if(word_json_get_int(json, key, &int_value) == 0) {
        *out = (unsigned int)int_value & 0x00ffffff;
        return 0;
    }

    return -3;
}

/**
 * @brief 写入文字模块默认配置。
 *
 * @param[out] config 要写入的配置对象。
 *
 * @details 读取 JSON 前先设置默认值，这样配置文件可以只覆盖部分字段。
 */
static void word_config_set_defaults(word_config_t *config)
{
    if(config == NULL)
        return;

    snprintf(config->zh_font_path, sizeof(config->zh_font_path),
             "%s", WORD_DEFAULT_ZH_FONT);
    snprintf(config->west_font_path, sizeof(config->west_font_path),
             "%s", WORD_DEFAULT_WEST_FONT);
    config->zh_font_index = 0;
    config->west_font_index = 0;
    config->size = 32;
    config->line_gap = 6;
    config->color = 0x00ffffff;
}

/**
 * @brief 从 JSON 文件加载文字配置。
 *
 * @param[out] config 输出配置对象。
 *
 * @retval 0 加载成功。
 * @retval -1 参数无效。
 * @retval -2 JSON 文件读取失败。
 *
 * @details 该函数固定读取 font/word.json。读取前先填入默认值，再读取
 * JSON 中存在且有效的字段。字号必须大于 0，行距和字体索引必须非负；
 * 无效字段会被忽略并保留默认值。
 */
static int word_config_load(word_config_t *config)
{
    unsigned char *json_data = NULL;
    long json_size = 0;
    int value;
    unsigned int color;

    if(config == NULL)
        return -1;

    word_config_set_defaults(config);

    if(word_read_file(WORD_DEFAULT_CONFIG, &json_data, &json_size) != 0)
        return -2;

    word_json_get_string((const char *)json_data, "zh",
                         config->zh_font_path, sizeof(config->zh_font_path));
    word_json_get_string((const char *)json_data, "west",
                         config->west_font_path, sizeof(config->west_font_path));

    if(word_json_get_int((const char *)json_data, "zh_index", &value) == 0 &&
       value >= 0)
        config->zh_font_index = value;

    if(word_json_get_int((const char *)json_data, "west_index", &value) == 0 &&
       value >= 0)
        config->west_font_index = value;

    if(word_json_get_int((const char *)json_data, "size", &value) == 0 &&
       value > 0)
        config->size = value;

    if(word_json_get_int((const char *)json_data, "line_gap", &value) == 0 &&
       value >= 0)
        config->line_gap = value;

    if(word_json_get_color((const char *)json_data, "color", &color) == 0)
        config->color = color;

    free(json_data);
    return 0;
}

/**
 * @brief 从 UTF-8 字符串中解码下一个 Unicode codepoint。
 *
 * @param[in,out] text 输入字符串游标。成功解码后会向后移动。
 * @param[out] codepoint 输出 Unicode codepoint。
 *
 * @retval 1 解码得到一个字符。
 * @retval 0 参数无效或已经到字符串结尾。
 *
 * @details 支持 1 到 4 字节 UTF-8。遇到不合法字节序列时输出 '?'，
 * 并只跳过当前 1 字节，避免整个字符串显示中断。
 */
static int word_utf8_next(const char **text, unsigned int *codepoint)
{
    const unsigned char *p;

    if(text == NULL || *text == NULL || codepoint == NULL)
        return 0;

    p = (const unsigned char *)*text;
    if(*p == '\0')
        return 0;

    if(p[0] < 0x80) {
        *codepoint = p[0];
        *text += 1;
        return 1;
    }

    if((p[0] & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) {
        *codepoint = ((unsigned int)(p[0] & 0x1f) << 6) |
                     (unsigned int)(p[1] & 0x3f);
        *text += 2;
        return 1;
    }

    if((p[0] & 0xf0) == 0xe0 &&
       (p[1] & 0xc0) == 0x80 &&
       (p[2] & 0xc0) == 0x80) {
        *codepoint = ((unsigned int)(p[0] & 0x0f) << 12) |
                     ((unsigned int)(p[1] & 0x3f) << 6) |
                     (unsigned int)(p[2] & 0x3f);
        *text += 3;
        return 1;
    }

    if((p[0] & 0xf8) == 0xf0 &&
       (p[1] & 0xc0) == 0x80 &&
       (p[2] & 0xc0) == 0x80 &&
       (p[3] & 0xc0) == 0x80) {
        *codepoint = ((unsigned int)(p[0] & 0x07) << 18) |
                     ((unsigned int)(p[1] & 0x3f) << 12) |
                     ((unsigned int)(p[2] & 0x3f) << 6) |
                     (unsigned int)(p[3] & 0x3f);
        *text += 4;
        return 1;
    }

    *codepoint = '?';
    *text += 1;
    return 1;
}

/**
 * @brief 判断 codepoint 是否优先使用西文字体。
 *
 * @param[in] codepoint Unicode codepoint。
 *
 * @retval 1 属于 ASCII、Latin、扩展拉丁等西文范围。
 * @retval 0 其他字符，优先使用中文字体。
 */
static int word_is_western(unsigned int codepoint)
{
    return codepoint <= 0x024f;
}

/**
 * @brief 根据字符选择要使用的字体。
 *
 * @param[in] codepoint Unicode codepoint。
 *
 * @retval 非NULL 可用于该字符的字体对象。
 * @retval NULL 没有可用字体。
 *
 * @details 西文优先使用 Consolas，中文和中文标点优先使用宋体。
 * 如果首选字体缺少该字符，会尝试另一个字体；仍然缺字时最后回退到
 * 已加载的西文字体或中文字体，由调用者再把字符替换成 '?'。
 */
static word_font_t *word_select_font(unsigned int codepoint)
{
    word_font_t *primary = word_is_western(codepoint) ?
                           &word_west_font : &word_zh_font;
    word_font_t *fallback = primary == &word_west_font ?
                            &word_zh_font : &word_west_font;

    if(primary->loaded &&
       (codepoint == ' ' || stbtt_FindGlyphIndex(&primary->info, codepoint) != 0))
        return primary;

    if(fallback->loaded &&
       (codepoint == ' ' || stbtt_FindGlyphIndex(&fallback->info, codepoint) != 0))
        return fallback;

    if(word_west_font.loaded)
        return &word_west_font;

    return word_zh_font.loaded ? &word_zh_font : NULL;
}

/**
 * @brief 将前景文字像素按 alpha 混合到背景像素上。
 *
 * @param[in] bg framebuffer 中已有的背景像素，格式 0x00RRGGBB。
 * @param[in] fg 文字前景颜色，格式 0x00RRGGBB。
 * @param[in] alpha 字形灰度 alpha，0 表示透明，255 表示完全覆盖。
 *
 * @return 混合后的 0x00RRGGBB 像素。
 */
static unsigned int word_blend_pixel(unsigned int bg,
                                     unsigned int fg,
                                     unsigned char alpha)
{
    unsigned int bg_r = (bg >> 16) & 0xff;
    unsigned int bg_g = (bg >> 8) & 0xff;
    unsigned int bg_b = bg & 0xff;
    unsigned int fg_r = (fg >> 16) & 0xff;
    unsigned int fg_g = (fg >> 8) & 0xff;
    unsigned int fg_b = fg & 0xff;
    unsigned int out_r = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
    unsigned int out_g = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
    unsigned int out_b = (fg_b * alpha + bg_b * (255 - alpha)) / 255;

    return (out_r << 16) | (out_g << 8) | out_b;
}

/**
 * @brief 计算当前字号对应的 stb_truetype 缩放比例。
 *
 * @param[in] font 已加载字体。
 *
 * @return stb_truetype 使用的 scale 值。
 */
static float word_font_scale(const word_font_t *font)
{
    return stbtt_ScaleForPixelHeight(&font->info, (float)word_config.size);
}

/**
 * @brief 计算字体 ascent 对应的像素高度。
 *
 * @param[in] font 已加载字体。
 *
 * @return ascent 像素高度。字体不可用时返回当前字号。
 *
 * @details 显示接口的 y 坐标表示外接矩形上边界，而 stb_truetype 绘制需要
 * baseline。这里用 ascent 把上边界换算为 baseline。
 */
static int word_font_ascent_px(const word_font_t *font)
{
    int ascent;
    int descent;
    int line_gap;
    float scale;

    if(font == NULL || !font->loaded)
        return word_config.size;

    scale = word_font_scale(font);
    stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &line_gap);

    return (int)(ascent * scale + 0.5f);
}

/**
 * @brief 返回当前中西文字体中较大的 ascent。
 *
 * @return 行 baseline 相对文字上边界的像素偏移。
 *
 * @details 同一行可能混排中文和西文，使用较大的 ascent 可以减少不同字体
 * 混排时被顶部裁剪的风险。
 */
static int word_line_ascent(void)
{
    int zh_ascent = word_font_ascent_px(&word_zh_font);
    int west_ascent = word_font_ascent_px(&word_west_font);

    return zh_ascent > west_ascent ? zh_ascent : west_ascent;
}

/**
 * @brief 返回一行文字的推进高度。
 *
 * @return 字号加配置行距后的行高。
 */
static int word_line_height(void)
{
    return word_config.size + word_config.line_gap;
}

/**
 * @brief 计算单个字符绘制后的水平推进量。
 *
 * @param[in] font 字符所使用的字体。
 * @param[in] codepoint Unicode codepoint。
 *
 * @return 当前字号下的水平 advance 像素值。
 */
static int word_glyph_advance(word_font_t *font, unsigned int codepoint)
{
    int advance;
    int lsb;

    if(font == NULL || !font->loaded)
        return 0;

    stbtt_GetCodepointHMetrics(&font->info, codepoint, &advance, &lsb);
    return (int)(advance * word_font_scale(font) + 0.5f);
}

/**
 * @brief 在 framebuffer 上绘制一个 Unicode 字符。
 *
 * @param[in,out] fb LCD framebuffer 二维像素数组。
 * @param[in,out] cursor_x 当前绘制游标横坐标，函数结束后推进到下个字符位置。
 * @param[in] baseline_y 当前行 baseline 的纵坐标。
 * @param[in] codepoint 要绘制的 Unicode codepoint。
 * @param[in] color 文字颜色，格式 0x00RRGGBB。
 *
 * @details 函数会根据 codepoint 选择字体，使用 stb_truetype 生成灰度位图，
 * 再逐像素 alpha 混合到 framebuffer。绘制时会裁剪到 LCD 范围内，屏幕外
 * 像素不会写入。即使字符缺字，也会尝试使用 '?' 替代并正常推进游标。
 */
static void word_draw_codepoint(unsigned int (*fb)[LCD_W],
                                int *cursor_x,
                                int baseline_y,
                                unsigned int codepoint,
                                unsigned int color)
{
    word_font_t *font = word_select_font(codepoint);
    float scale;
    int bmp_w;
    int bmp_h;
    int xoff;
    int yoff;
    int bx;
    int by;
    unsigned char *bitmap;

    if(font == NULL || cursor_x == NULL)
        return;

    if(stbtt_FindGlyphIndex(&font->info, codepoint) == 0)
        codepoint = '?';

    scale = word_font_scale(font);
    bitmap = stbtt_GetCodepointBitmap(&font->info, 0.0f, scale,
                                      codepoint, &bmp_w, &bmp_h, &xoff, &yoff);

    if(bitmap != NULL) {
        for(by = 0; by < bmp_h; by++) {
            int dst_y = baseline_y + yoff + by;

            if(dst_y < 0 || dst_y >= LCD_H)
                continue;

            for(bx = 0; bx < bmp_w; bx++) {
                int dst_x = *cursor_x + xoff + bx;
                unsigned char alpha = bitmap[by * bmp_w + bx];

                if(alpha == 0 || dst_x < 0 || dst_x >= LCD_W)
                    continue;

                fb[dst_y][dst_x] = word_blend_pixel(fb[dst_y][dst_x],
                                                     color,
                                                     alpha);
            }
        }

        stbtt_FreeBitmap(bitmap, NULL);
    }

    *cursor_x += word_glyph_advance(font, codepoint);
}

/**
 * @brief 重新加载文字配置和两套字体。
 *
 * @retval 0 加载成功。
 * @retval -1 配置加载失败。
 * @retval -2 中文字体加载失败。
 * @retval -3 西文字体加载失败。
 *
 * @details 函数固定读取 font/word.json。为避免加载失败后留下半初始化
 * 状态，函数先加载到局部临时对象。两套字体都成功后，才释放旧字体并
 * 切换到新配置。
 */
int lcd_word_reload(void)
{
    word_config_t new_config;
    word_font_t new_zh_font = {NULL, 0, {0}, 0};
    word_font_t new_west_font = {NULL, 0, {0}, 0};

    if(word_config_load(&new_config) != 0)
        return -1;

    if(word_font_load(&new_zh_font,
                      new_config.zh_font_path,
                      new_config.zh_font_index) != 0) {
        word_font_free(&new_zh_font);
        return -2;
    }

    if(word_font_load(&new_west_font,
                      new_config.west_font_path,
                      new_config.west_font_index) != 0) {
        word_font_free(&new_zh_font);
        word_font_free(&new_west_font);
        return -3;
    }

    word_font_free(&word_zh_font);
    word_font_free(&word_west_font);
    word_zh_font = new_zh_font;
    word_west_font = new_west_font;
    word_config = new_config;
    word_ready = 1;

    return 0;
}

/**
 * @brief 初始化文字模块。
 *
 * @retval 0 初始化成功。
 * @retval <0 透传 lcd_word_reload() 的错误码。
 */
int lcd_word_init(void)
{
    return lcd_word_reload();
}

/**
 * @brief 使用指定颜色绘制 UTF-8 字符串。
 *
 * @param[in] x 文字外接矩形左上角横坐标。
 * @param[in] y 文字外接矩形左上角纵坐标。
 * @param[in] utf8 UTF-8 字符串。
 * @param[in] color 文字颜色，格式 0x00RRGGBB。
 *
 * @retval 0 绘制成功。
 * @retval -1 LCD framebuffer 尚未初始化。
 * @retval -2 文字模块尚未初始化。
 * @retval -3 utf8 参数为空。
 *
 * @details y 会先转换成 baseline，再逐字符绘制。'\n' 会换到下一行，
 * '\r' 会被忽略，'\t' 会按一个字号宽度推进。函数绘制到 LCD 当前
 * 绘制目标，不会清除文字区域背景。
 */
int lcd_word_show_color(int x, int y, const char *utf8, unsigned int color)
{
    unsigned int (*fb)[LCD_W] = lcd_get_draw_p();
    const char *p = utf8;
    int cursor_x = x;
    int baseline_y = y + word_line_ascent();
    unsigned int codepoint;

    if(fb == NULL)
        return -1;

    if(!word_ready)
        return -2;

    if(utf8 == NULL)
        return -3;

    while(word_utf8_next(&p, &codepoint)) {
        if(codepoint == '\r')
            continue;

        if(codepoint == '\n') {
            cursor_x = x;
            baseline_y += word_line_height();
            continue;
        }

        if(codepoint == '\t') {
            cursor_x += word_config.size;
            continue;
        }

        word_draw_codepoint(fb, &cursor_x, baseline_y,
                            codepoint, color & 0x00ffffff);
    }

    return 0;
}

/**
 * @brief 使用配置中的默认颜色绘制 UTF-8 字符串。
 *
 * @param[in] x 文字外接矩形左上角横坐标。
 * @param[in] y 文字外接矩形左上角纵坐标。
 * @param[in] utf8 UTF-8 字符串。
 *
 * @retval 0 绘制成功。
 * @retval <0 透传 lcd_word_show_color() 的错误码。
 */
int lcd_word_show(int x, int y, const char *utf8)
{
    return lcd_word_show_color(x, y, utf8, word_config.color);
}

/**
 * @brief 估算 UTF-8 字符串绘制尺寸。
 *
 * @param[in] utf8 UTF-8 字符串。
 * @param[out] length 输出宽度，可为 NULL。
 * @param[out] width 输出高度，可为 NULL。
 *
 * @retval 0 估算成功。
 * @retval -1 文字模块尚未初始化。
 * @retval -2 utf8 参数为空。
 *
 * @details 宽度使用字符 advance 累加，不考虑字形实际外接框左右超出。
 * 高度按行数和 line_height 估算，适合提前清屏或布局。
 */
int lcd_word_measure(const char *utf8, int *length, int *width)
{
    const char *p = utf8;
    int cursor_x = 0;
    int max_x = 0;
    int lines = 1;
    unsigned int codepoint;

    if(!word_ready)
        return -1;

    if(utf8 == NULL)
        return -2;

    while(word_utf8_next(&p, &codepoint)) {
        word_font_t *font;

        if(codepoint == '\r')
            continue;

        if(codepoint == '\n') {
            if(cursor_x > max_x)
                max_x = cursor_x;

            cursor_x = 0;
            lines++;
            continue;
        }

        if(codepoint == '\t') {
            cursor_x += word_config.size;
            continue;
        }

        font = word_select_font(codepoint);
        if(font != NULL) {
            if(stbtt_FindGlyphIndex(&font->info, codepoint) == 0)
                codepoint = '?';

            cursor_x += word_glyph_advance(font, codepoint);
        }
    }

    if(cursor_x > max_x)
        max_x = cursor_x;

    if(length != NULL)
        *length = max_x;

    if(width != NULL)
        *width = lines * word_line_height() - word_config.line_gap;

    return 0;
}

/**
 * @brief 释放文字模块资源。
 *
 * @details 会释放中文和西文字体缓冲区，恢复默认配置，并把 word_ready
 * 置为 0。重复调用是安全的。
 */
void lcd_word_uninit(void)
{
    word_font_free(&word_zh_font);
    word_font_free(&word_west_font);
    word_config_set_defaults(&word_config);
    word_ready = 0;
}
