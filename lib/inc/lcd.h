#ifndef __LCD_H__
#define __LCD_H__

#define LCD_W 800
#define LCD_H 480
#define LCD_SIZE (LCD_W * LCD_H * 4)
#define LCD_BLACK 0x00000000

int lcd_init(void);
void lcd_show(int x, int y, unsigned int color);
int lcd_show_pic_full(const char *pic_path);
int lcd_show_pic_rect(const char *pic_path, int x, int y, int length, int width);
void lcd_clear(void);
void lcd_uninit(void);

unsigned int (*lcd_get_p(void))[LCD_W];

#define LCD_SHOW_PIC_SELECT(_1, _2, _3, _4, _5, NAME, ...) NAME
#define lcd_show_pic(...)                                                     \
    LCD_SHOW_PIC_SELECT(__VA_ARGS__,                                          \
                        lcd_show_pic_rect,                                    \
                        lcd_show_pic_need_1_or_5_args,                       \
                        lcd_show_pic_need_1_or_5_args,                       \
                        lcd_show_pic_need_1_or_5_args,                       \
                        lcd_show_pic_full)(__VA_ARGS__)

#endif
