#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>

#define SW 800
#define SH 480

#define RED    0x00DE2910
#define YELLOW 0x00FFDE00
#define BLACK  0x00000000

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int in_poly(double x, double y, double vx[], double vy[], int n)
{
    int i, j, c = 0;

    for(i = 0, j = n - 1; i < n; j = i++){
        if(((vy[i] > y) != (vy[j] > y)) &&
           (x < (vx[j] - vx[i]) * (y - vy[i]) / (vy[j] - vy[i]) + vx[i]))
            c = !c;
    }

    return c;
}

void make_star(double cx, double cy, double r, double rot, double vx[], double vy[])
{
    int i;
    double r2 = r * 0.38196601125;

    for(i = 0; i < 10; i++){
        double rr = (i % 2 == 0) ? r : r2;
        double a = rot + i * M_PI / 5.0;
        vx[i] = cx + rr * cos(a);
        vy[i] = cy + rr * sin(a);
    }
}

int main()
{
    int lcd_fd = open("/dev/fb0", O_RDWR);
    int i, j, k;

    int fw = 720;
    int fh = 480;
    int ox = 40;
    int oy = 0;

    double u = fh / 20.0;

    double cx[5], cy[5], r[5], rot[5];
    double vx[5][10], vy[5][10];

    unsigned int color;

    cx[0] = ox + 5 * u;
    cy[0] = oy + 5 * u;
    r[0] = 3 * u;
    rot[0] = -M_PI / 2.0;

    cx[1] = ox + 10 * u;
    cy[1] = oy + 2 * u;
    r[1] = 1 * u;
    rot[1] = atan2(cy[0] - cy[1], cx[0] - cx[1]);

    cx[2] = ox + 12 * u;
    cy[2] = oy + 4 * u;
    r[2] = 1 * u;
    rot[2] = atan2(cy[0] - cy[2], cx[0] - cx[2]);

    cx[3] = ox + 12 * u;
    cy[3] = oy + 7 * u;
    r[3] = 1 * u;
    rot[3] = atan2(cy[0] - cy[3], cx[0] - cx[3]);

    cx[4] = ox + 10 * u;
    cy[4] = oy + 9 * u;
    r[4] = 1 * u;
    rot[4] = atan2(cy[0] - cy[4], cx[0] - cx[4]);

    for(k = 0; k < 5; k++)
        make_star(cx[k], cy[k], r[k], rot[k], vx[k], vy[k]);

    lseek(lcd_fd, 0, SEEK_SET);

    for(i = 0; i < SH; i++){
        for(j = 0; j < SW; j++){
            color = BLACK;

            if(j >= ox && j < ox + fw && i >= oy && i < oy + fh)
                color = RED;

            for(k = 0; k < 5; k++){
                if(in_poly(j + 0.5, i + 0.5, vx[k], vy[k], 10)){
                    color = YELLOW;
                    break;
                }
            }

            write(lcd_fd, &color, 4);
        }
    }

    close(lcd_fd);
    return 0;
}