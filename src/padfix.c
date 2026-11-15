// padfix.so (native armhf, LD_PRELOAD on box86): gives Hotline Miami's own Xbox-pad support a pad it understands.
// The game (PhyreEngine PInputDevicePadJoystick) reads /dev/input/js0 raw and expects the xpad layout:
//   ABS_X/Y left stick, ABS_RX/RY right stick, ABS_Z/RZ = LT/RT, full +-32767 range, hats ignored;
//   BTN_A, BTN_B, BTN_X(0x133), BTN_Y(0x134), TL, TR, SELECT, START, THUMBL, THUMBR.
// The RG34XX "ANBERNIC-keys" device has sticks on Z/RX (left) + RY/RZ (right) at +-4096, L2/R2 as buttons and a
// hat d-pad, so the game sees garbage. We create a uinput "Microsoft X-Box 360 pad", feed it translated events from
// the real device (thread), and redirect the game's open("/dev/input/js0") to the virtual pad's js node.
// Positions follow the PortMaster SDL mapping of the device (a:b0 b:b1 y:b2 x:b3 L1:b4 R1:b5 guide:b6 start:b7
// back:b8 L3:b9 L2:b10 R2:b11 R3:b12). D-pad also drives the left stick (menus).
// box86's wrapped open() calls the native open through the PLT, so LD_PRELOAD interposes it.
// Env: PADFIX_LOG=1, PADFIX_GAIN (stick gain, default 8), PADFIX_SWAPAB=1 (swap A/B and X/Y), PADFIX=0 disables.
#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>

#define EXP __attribute__((visibility("default")))
static int dbg, gain = 8, swapab;
static int src = -1, ui = -1;
static char vjs[64];              // "/dev/input/jsN" of the virtual pad
static char realjs[64];           // the real device's js node (for the log)

static void L(const char *f, ...) {
    va_list a; va_start(a, f); fprintf(stderr, "[padfix] "); vfprintf(stderr, f, a); fputc('\n', stderr); va_end(a);
}

static void emit(int type, int code, int val) {
    struct input_event e; memset(&e, 0, sizeof e);
    e.type = type; e.code = code; e.value = val;
    long r = syscall(SYS_write, ui, &e, sizeof e);   // raw syscall: box86 may interpose write()
    if (dbg > 1 || (r != sizeof e && dbg)) L("uinput write %d %#x %d = %ld %s", type, code, val, r, r < 0 ? strerror(errno) : "");
}
static int clampi(int v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : v; }

static pthread_mutex_t tm = PTHREAD_MUTEX_INITIALIZER;
static void translate(struct input_event *e) {
    static int hx, hy, lx, ly;
    pthread_mutex_lock(&tm);
            if (dbg) L("in %d %#x %d", e->type, e->code, e->value);
            if (e->type == EV_SYN) emit(EV_SYN, SYN_REPORT, 0);
            if (e->type == EV_ABS) {
                switch (e->code) {
                case ABS_Z:  lx = clampi(e->value * gain); emit(EV_ABS, ABS_X, hx ? hx * 32767 : lx); break;
                case ABS_RX: ly = clampi(e->value * gain); emit(EV_ABS, ABS_Y, hy ? hy * 32767 : ly); break;
                case ABS_RY: emit(EV_ABS, ABS_RX, clampi(e->value * gain)); break;
                case ABS_RZ: emit(EV_ABS, ABS_RY, clampi(e->value * gain)); break;
                case ABS_HAT0X: hx = e->value; emit(EV_ABS, ABS_HAT0X, hx); emit(EV_ABS, ABS_X, hx ? hx * 32767 : lx); break;
                case ABS_HAT0Y: hy = e->value; emit(EV_ABS, ABS_HAT0Y, hy); emit(EV_ABS, ABS_Y, hy ? hy * 32767 : ly); break;
                }
            } else if (e->type == EV_KEY) {
                int v = e->value ? 1 : 0, c = -1;
                switch (e->code) {
                case 0x130: c = swapab ? BTN_B : BTN_A; break;
                case 0x131: c = swapab ? BTN_A : BTN_B; break;
                case 0x132: c = swapab ? BTN_X : BTN_Y; break;   // north
                case 0x133: c = swapab ? BTN_Y : BTN_X; break;   // west
                case 0x134: c = BTN_TL; break;
                case 0x135: c = BTN_TR; break;
                case 0x137: c = BTN_START; break;
                case 0x138: c = BTN_SELECT; break;
                case 0x139: c = BTN_THUMBL; break;
                case 0x13c: c = BTN_THUMBR; break;
                case 0x13a: emit(EV_ABS, ABS_Z, v ? 255 : 0); break;
                case 0x13b: emit(EV_ABS, ABS_RZ, v ? 255 : 0); break;
                }
                if (c >= 0) emit(EV_KEY, c, v);
            }
    pthread_mutex_unlock(&tm);
}
static void *pump(void *arg) {
    struct input_event ev[32];
    for (;;) {
        ssize_t n = read(src, ev, sizeof ev);
        if (n < 0) { if (errno == EINTR) continue; L("read: %s", strerror(errno)); return NULL; }
        for (int i = 0; i < n / (ssize_t)sizeof *ev; i++) translate(&ev[i]);
    }
}
// PADFIX_SCRIPT="t:type:code:val[:hold_ms] ..." (seconds since start, source-device codes, e.g. 20:1:0x130:1:150 = tap
// A at 20 s; 25:3:2:3000:2000 = left stick right for 2 s) - for unattended tests
static void *script(void *arg) {
    char *s = strdup(arg), *save = NULL; double t0 = 0;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); t0 = ts.tv_sec + ts.tv_nsec / 1e9;
    for (char *tok = strtok_r(s, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) {
        double t; int type, code, val, hold = 120;
        if (sscanf(tok, "%lf:%i:%i:%i:%i", &t, &type, &code, &val, &hold) < 4) continue;
        for (;;) { clock_gettime(CLOCK_MONOTONIC, &ts); double d = t0 + t - (ts.tv_sec + ts.tv_nsec / 1e9); if (d <= 0) break; usleep(d > 0.05 ? 50000 : (useconds_t)(d * 1e6)); }
        struct input_event e = { .type = type, .code = code, .value = val }, y = { .type = EV_SYN };
        translate(&e); translate(&y);
        L("script t=%.1f %d %#x %d hold %d", t, type, code, val, hold);
        usleep(hold * 1000);
        e.value = 0; translate(&e); translate(&y);
    }
    return NULL;
}

static int find_src(void) {
    for (int i = 0; i < 16; i++) {
        char p[64], name[128] = ""; snprintf(p, sizeof p, "/dev/input/event%d", i);
        int fd = open(p, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        ioctl(fd, EVIOCGNAME(sizeof name), name);
        if (!strcmp(name, "ANBERNIC-keys")) {
            // remember its js node
            char sp[128]; snprintf(sp, sizeof sp, "/sys/class/input/event%d/device", i);
            DIR *d = opendir(sp); struct dirent *de;
            while (d && (de = readdir(d))) if (!strncmp(de->d_name, "js", 2)) snprintf(realjs, sizeof realjs, "/dev/input/%s", de->d_name);
            if (d) closedir(d);
            if (dbg) L("source %s (%s) js %s", p, name, realjs);
            return fd;
        }
        close(fd);
    }
    return -1;
}

static void absset(struct uinput_user_dev *u, int code, int mn, int mx, int flat) {
    ioctl(ui, UI_SET_ABSBIT, code);
    u->absmin[code] = mn; u->absmax[code] = mx; u->absflat[code] = flat;
}

__attribute__((constructor)) static void padfix_init(void) {
    const char *e = getenv("PADFIX");
    if (e && !atoi(e)) return;
    // only in the game process (box86), not in shells/tools that inherit LD_PRELOAD
    char exe[256] = ""; ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1); if (n > 0) exe[n] = 0;
    if (!strstr(exe, "box86")) return;
    dbg = getenv("PADFIX_LOG") ? atoi(getenv("PADFIX_LOG")) : 0;
    if (getenv("PADFIX_GAIN")) gain = atoi(getenv("PADFIX_GAIN"));
    swapab = getenv("PADFIX_SWAPAB") ? atoi(getenv("PADFIX_SWAPAB")) : 0;
    unsetenv("LD_PRELOAD");

    if ((src = find_src()) < 0) { L("ANBERNIC-keys not found"); return; }
    if ((ui = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC)) < 0) { L("uinput: %s", strerror(errno)); return; }
    struct uinput_user_dev u; memset(&u, 0, sizeof u);
    snprintf(u.name, sizeof u.name, "Microsoft X-Box 360 pad");
    u.id.bustype = BUS_USB; u.id.vendor = 0x045e; u.id.product = 0x028e; u.id.version = 0x110;
    ioctl(ui, UI_SET_EVBIT, EV_KEY); ioctl(ui, UI_SET_EVBIT, EV_ABS); ioctl(ui, UI_SET_EVBIT, EV_SYN);
    static const int keys[] = { BTN_A, BTN_B, BTN_X, BTN_Y, BTN_TL, BTN_TR, BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR };
    for (unsigned i = 0; i < sizeof keys / sizeof *keys; i++) ioctl(ui, UI_SET_KEYBIT, keys[i]);
    absset(&u, ABS_X, -32768, 32767, 128); absset(&u, ABS_Y, -32768, 32767, 128);
    absset(&u, ABS_RX, -32768, 32767, 128); absset(&u, ABS_RY, -32768, 32767, 128);
    absset(&u, ABS_Z, 0, 255, 0); absset(&u, ABS_RZ, 0, 255, 0);
    absset(&u, ABS_HAT0X, -1, 1, 0); absset(&u, ABS_HAT0Y, -1, 1, 0);
    if (write(ui, &u, sizeof u) != sizeof u || ioctl(ui, UI_DEV_CREATE)) { L("uinput create: %s", strerror(errno)); close(ui); ui = -1; return; }

    char sys[64] = "", sp[160];
    if (ioctl(ui, UI_GET_SYSNAME(sizeof sys), sys) < 0) L("UI_GET_SYSNAME: %s", strerror(errno));
    snprintf(sp, sizeof sp, "/sys/devices/virtual/input/%s", sys);
    for (int t = 0; t < 100 && !vjs[0]; t++) {
        DIR *d = opendir(sp); struct dirent *de;
        while (d && (de = readdir(d))) if (!strncmp(de->d_name, "js", 2)) snprintf(vjs, sizeof vjs, "/dev/input/%s", de->d_name);
        if (d) closedir(d);
        if (vjs[0]) for (int w = 0; w < 100 && access(vjs, R_OK); w++) usleep(10000);
        else usleep(10000);
    }
    L("virtual xpad %s -> %s (game's %s redirected)", sys, vjs[0] ? vjs : "?", realjs[0] ? realjs : "?");
    pthread_t th; pthread_create(&th, NULL, pump, NULL);
    if (getenv("PADFIX_SCRIPT")) pthread_create(&th, NULL, script, strdup(getenv("PADFIX_SCRIPT")));
}

// The game's PhyreEngine pad opens /dev/input/js<joystick= from hotline.cfg> with O_CREAT (x86 0x40, same on arm):
// hand it the virtual pad. Every other js open in the process (the firmware SDL's evdev scan in x11shim, which would
// parse js events as input_events and inject junk keys) gets ENOENT.
static const char *redir(const char *p, int fl) {
    if (!p || !vjs[0] || strncmp(p, "/dev/input/js", 13)) return p;
    if (fl & O_CREAT) { if (dbg) L("game open %s -> %s", p, vjs); return vjs; }
    if (dbg) L("hide %s (flags %#x)", p, fl);
    return "/dev/input/padfix-hidden";
}
#define OPENBODY(call) \
    mode_t m = 0; if (fl & O_CREAT) { va_list a; va_start(a, fl); m = va_arg(a, int); va_end(a); } \
    const char *q = redir(p, fl); if (q != p) fl &= ~O_CREAT; \
    return call;
EXP int open(const char *p, int fl, ...) {
    static int (*r)(const char *, int, ...); if (!r) r = dlsym(RTLD_NEXT, "open");
    OPENBODY(r(q, fl, m))
}
EXP int open64(const char *p, int fl, ...) {
    static int (*r)(const char *, int, ...); if (!r) r = dlsym(RTLD_NEXT, "open64");
    OPENBODY(r(q, fl, m))
}
EXP int openat(int dfd, const char *p, int fl, ...) {
    static int (*r)(int, const char *, int, ...); if (!r) r = dlsym(RTLD_NEXT, "openat");
    OPENBODY(r(dfd, q, fl, m))
}
