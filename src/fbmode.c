#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
int main(int c, char **v) {
    int fd = open("/dev/fb0", O_RDWR); struct fb_var_screeninfo s; ioctl(fd, FBIOGET_VSCREENINFO, &s);
    printf("was %ux%u virt %ux%u @%u\n", s.xres, s.yres, s.xres_virtual, s.yres_virtual, s.bits_per_pixel);
    if (c < 4) return 0;
    s.xres = s.xres_virtual = atoi(v[1]); s.yres = atoi(v[2]); s.yres_virtual = s.yres * 2; s.bits_per_pixel = atoi(v[3]);
    s.xoffset = s.yoffset = 0;
    if (s.bits_per_pixel == 16) { s.red.offset = 11; s.red.length = 5; s.green.offset = 5; s.green.length = 6; s.blue.offset = 0; s.blue.length = 5; s.transp.offset = s.transp.length = 0; }
    int r = ioctl(fd, FBIOPUT_VSCREENINFO, &s); ioctl(fd, FBIOGET_VSCREENINFO, &s);
    printf("set %d -> %ux%u virt %ux%u @%u\n", r, s.xres, s.yres, s.xres_virtual, s.yres_virtual, s.bits_per_pixel);
}
