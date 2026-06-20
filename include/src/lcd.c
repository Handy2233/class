#include "lcd.h"
#include "log.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

static int lcd_fd = -1;
static unsigned int (*lcd_p)[LCD_W] = NULL;

int lcd_init(void)
{
    lcd_fd = open("/dev/fb0", O_RDWR);
    if(lcd_fd == -1) {
        LOG_PERROR("open /dev/fb0");
        return -1;
    }

    lcd_p = mmap(NULL, LCD_SIZE,
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED,
                 lcd_fd,
                 0);

    if(lcd_p == MAP_FAILED) {
        LOG_PERROR("mmap");
        close(lcd_fd);
        lcd_fd = -1;
        lcd_p = NULL;
        return -2;
    }
    
    return 0;
}

void lcd_show(int x, int y, unsigned int color)
{
    if(lcd_p == NULL)
        return;

    if(x < 0 || x >= LCD_W || y < 0 || y >= LCD_H)
        return;

    lcd_p[y][x] = color;
}

void lcd_clear(void)
{
    if(lcd_p == NULL)
        return;

    int x, y;

    for(y = 0; y < LCD_H; y++) {
        for(x = 0; x < LCD_W; x++) {
            lcd_p[y][x] = LCD_BLACK;
        }
    }
}

void lcd_uninit(void)
{
    if(lcd_p != NULL) {
        munmap(lcd_p, LCD_SIZE);
        lcd_p = NULL;
    }

    if(lcd_fd != -1) {
        close(lcd_fd);
        lcd_fd = -1;
    }
}

unsigned int (*lcd_get_p(void))[LCD_W]
{
    return lcd_p;
}
