#ifndef __WORD_H__
#define __WORD_H__

/**
 * @file word.h
 * @brief UTF-8 中英文文字显示接口。
 *
 * @details 该模块负责读取 font/word.json 配置，加载中文和西文字体，
 * 并把 UTF-8 字符串渲染到 LCD framebuffer。当前默认配置使用宋体
 * 显示中文，使用 Consolas 显示西文。
 *
 * @details 配置文件约定：
 * - size 表示字号像素高度。
 * - line_gap 表示多行文字之间的额外行距。
 * - color 表示默认文字颜色，格式为 0x00RRGGBB 或 "#RRGGBB" 字符串。
 * - font.zh 表示中文字体文件路径。
 * - font.west 表示西文字体文件路径。
 *
 * @details 调用顺序：
 * 1. 先调用 lcd_init() 初始化 LCD framebuffer。
 * 2. 再调用 lcd_word_init() 从 font/word.json 加载字体。
 * 3. 使用 lcd_word_show() 或 lcd_word_show_color() 绘制文字。
 * 4. 程序退出或不再显示文字时调用 lcd_word_uninit() 释放字体资源。
 *
 * @details 坐标约定：
 * - x/y 表示文字外接矩形左上角坐标，不是字体 baseline。
 * - 字符串支持 '\n' 换行，'\r' 会被忽略，'\t' 会按一个字号宽度推进。
 * - 绘制会裁剪到 LCD 屏幕范围内，不会访问屏幕外 framebuffer。
 */

/**
 * @brief 初始化 UTF-8 文字显示模块。
 *
 * @retval 0 初始化成功。
 * @retval <0 配置读取、字体加载或字体解析失败。
 *
 * @details 函数固定读取 font/word.json。初始化成功后，后续
 * lcd_word_show() 会使用配置中的默认颜色和字号。重复调用会释放旧字体
 * 并加载新字体。
 */
int lcd_word_init(void);

/**
 * @brief 重新读取 JSON 配置并加载字体。
 *
 * @retval 0 加载成功。
 * @retval <0 加载失败，原有配置会被释放。
 *
 * @details 函数固定读取 font/word.json，适合运行时修改 JSON 后重新载入
 * 字号、颜色或字体路径。调用失败时文字模块会处于未就绪状态，调用者应
 * 根据返回值决定是否继续显示文字。
 */
int lcd_word_reload(void);

/**
 * @brief 在 LCD 上显示 UTF-8 字符串。
 *
 * @param[in] x 文字左上角横坐标。
 * @param[in] y 文字左上角纵坐标。
 * @param[in] utf8 UTF-8 字符串，支持 '\n' 换行。
 *
 * @retval 0 显示成功。
 * @retval -1 LCD 尚未初始化。
 * @retval -2 文字模块尚未初始化。
 * @retval -3 utf8 参数为空。
 *
 * @details 函数使用 JSON 配置中的默认颜色绘制文字。文字背景透明，
 * 字形灰度会按 alpha 混合到 framebuffer 当前像素上，因此可以直接
 * 叠加在图片或已有 UI 上。
 */
int lcd_word_show(int x, int y, const char *utf8);

/**
 * @brief 使用指定颜色显示 UTF-8 字符串。
 *
 * @param[in] x 文字左上角横坐标。
 * @param[in] y 文字左上角纵坐标。
 * @param[in] utf8 UTF-8 字符串，支持 '\n' 换行。
 * @param[in] color 0x00RRGGBB 格式文字颜色。
 *
 * @retval 0 显示成功。
 * @retval -1 LCD 尚未初始化。
 * @retval -2 文字模块尚未初始化。
 * @retval -3 utf8 参数为空。
 *
 * @details color 只使用低 24 位，格式为 0x00RRGGBB。该函数不会修改
 * JSON 配置中的默认颜色，只影响本次绘制。
 */
int lcd_word_show_color(int x, int y, const char *utf8, unsigned int color);

/**
 * @brief 估算 UTF-8 字符串绘制后的外接矩形尺寸。
 *
 * @param[in] utf8 UTF-8 字符串。
 * @param[out] length 输出文字宽度，可为 NULL。
 * @param[out] width 输出文字高度，可为 NULL。
 *
 * @retval 0 估算成功。
 * @retval -1 文字模块尚未初始化。
 * @retval -2 utf8 参数为空。
 *
 * @details 该函数只根据字体 advance 和换行估算文字占用尺寸，不实际
 * 写 framebuffer。length 或 width 可以传 NULL，表示调用者不关心该值。
 */
int lcd_word_measure(const char *utf8, int *length, int *width);

/**
 * @brief 释放文字显示模块加载的字体资源。
 *
 * @details 函数会释放中文和西文字体文件缓冲，并把模块状态恢复为未初始化。
 * 可安全重复调用。释放后如需继续显示文字，需要重新调用 lcd_word_init()。
 */
void lcd_word_uninit(void);

#endif
