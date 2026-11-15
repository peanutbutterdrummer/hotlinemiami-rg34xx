// fbcap <seconds> <fps> <out.rgb>: records the visible fb0 page (32bpp) as raw RGB24 frames into RAM, writes at the end.
// Prints WxH and the frame count. SIGINT/SIGTERM stops early and still writes.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <string.h>
static volatile int stop;
static void on(int s) { (void)s; stop = 1; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
int main(int c, char **v) {
    if (c < 4) { fprintf(stderr, "usage: fbcap secs fps out.rgb\n"); return 1; }
    double secs = atof(v[1]), fps = atof(v[2]);
    int fd = open("/dev/fb0", O_RDONLY); struct fb_var_screeninfo s; struct fb_fix_screeninfo f;
    ioctl(fd, FBIOGET_VSCREENINFO, &s); ioctl(fd, FBIOGET_FSCREENINFO, &f);
    if (s.bits_per_pixel != 32) { fprintf(stderr, "fb is %u bpp, need 32 (start the game first)\n", s.bits_per_pixel); return 1; }
    unsigned W = s.xres, H = s.yres; size_t fsz = (size_t)W * H * 3, n = (size_t)(secs * fps) + 1, k = 0;
    uint8_t *fb = mmap(0, f.smem_len, PROT_READ, MAP_SHARED, fd, 0);
    uint8_t *buf = malloc(fsz * n); if (!buf || fb == MAP_FAILED) { fprintf(stderr, "alloc/mmap failed\n"); return 1; }
    signal(SIGINT, on); signal(SIGTERM, on);
    double t0 = now(), next = t0;
    while (!stop && k < n) {
        ioctl(fd, FBIOGET_VSCREENINFO, &s);
        static uint8_t *tmp; if (!tmp) tmp = malloc((size_t)f.line_length * H);
        memcpy(tmp, fb + (size_t)s.yoffset * f.line_length, (size_t)f.line_length * H);
        const uint8_t *src = tmp; uint8_t *d = buf + k * fsz;
        for (unsigned y = 0; y < H; y++) { const uint32_t *p = (const uint32_t *)(src + y * f.line_length);
            for (unsigned x = 0; x < W; x++) { uint32_t q = p[x]; *d++ = q >> 16; *d++ = q >> 8; *d++ = q; } }
        k++; next += 1.0 / fps; double w = next - now(); if (w > 0) usleep(w * 1e6);
    }
    FILE *o = fopen(v[3], "wb"); fwrite(buf, fsz, k, o); fclose(o);
    printf("%ux%u %zu frames %.2f fps\n", W, H, k, k / (now() - t0));
    return 0;
}
