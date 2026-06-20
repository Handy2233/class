#ifndef __LCD_H__
#define __LCD_H__

#define LCD_W 800
#define LCD_H 480
#define LCD_SIZE (LCD_W * LCD_H * 4)
#define LCD_BLACK 0x00000000

int lcd_init(void);
void lcd_show(int x, int y, unsigned int color);
void lcd_clear(void);
void lcd_uninit(void);

unsigned int (*lcd_get_p(void))[LCD_W];

#endif