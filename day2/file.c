#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(){
    int lcd_fd = open("/dev/fb0", O_RDWR);
    int i, j;
    //1
    for(i = 1; i <= 96; i++)//循环800*480次
    {
        for(j = 1; j <= 800; j++){
            unsigned int color = 0x005BCEFA;
            write(lcd_fd, &color, 4);
        }
    }
    //2
    for(i = 1; i <= 96; i++)//循环800*480次
    {
        for(j = 1; j <= 800; j++){
            unsigned int color = 0x00F5A9B8;
            write(lcd_fd, &color, 4);
        }
    }
    //3
    for(i = 1; i <= 96; i++)//循环800*480次
    {
        for(j = 1; j <= 800; j++){
            unsigned int color = 0x00FFFFFF;
            write(lcd_fd, &color, 4);
        }
    }
    //4
    for(i = 1; i <= 96; i++)//循环800*480次
    {
        for(j = 1; j <= 800; j++){
            unsigned int color = 0x00F5A9B8;
            write(lcd_fd, &color, 4);
        }
    }
    //5
    for(i = 1; i <= 96; i++)//循环800*480次
    {
        for(j = 1; j <= 800; j++){
            unsigned int color = 0x005BCEFA;
            write(lcd_fd, &color, 4);
        }
    }
    return 0;
}