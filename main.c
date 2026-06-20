#include "GEC6818.h"

void draw_circle(unsigned int (*p)[LCD_W], int cx, int cy, int r, unsigned int color)
{
    int x, y;

    for(y = cy - r; y <= cy + r; y++) {
        for(x = cx - r; x <= cx + r; x++) {
            if(x < 0 || x >= LCD_W || y < 0 || y >= LCD_H)
                continue;

            if((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) {
                p[y][x] = color;
            }
        }
    }
}

int main(){
    lcd_init();
    lcd_clear();

    //draw_circle(lcd_get_p(), 400, 240, 100, 0x0000FF00);
    lcd_show_pic("./pic/1.png", 0, 0, 800, 480);

    lcd_uninit();
    return 0;
}
