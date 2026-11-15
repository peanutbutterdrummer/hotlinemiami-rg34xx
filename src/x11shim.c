// x11shim: a tiny fake Xlib + GLX for running Hotline Miami (GOG Linux, i386, via box86) on a
// Mali fbdev handheld with no X server. Built for armhf as libX11.so.6; box86 dlopens it as the
// native libX11 and (through hm1gl.so, which links it first) takes glX* from it too. Every window
// maps onto one fullscreen SDL2 window whose GL context comes from SDL (EGL) with gl4es as the
// GL provider (SDL_VIDEO_GL_DRIVER, LIBGL_FB=0). Keyboard state comes from SDL key events (gptokeyb
// or a real keyboard) and, optionally, from an SDL game controller mapped to keys (X11SHIM_PADKEYS).
// Only the ~35 calls the game imports are implemented. X11SHIM_LOG=1 logs calls.
#define _GNU_SOURCE
#define XLIB_ILLEGAL_ACCESS
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <GL/glx.h>
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

#define EXP __attribute__((visibility("default")))
static int dbg = -1;
static void L(const char *f, ...) {
    if (dbg < 0) dbg = getenv("X11SHIM_LOG") ? atoi(getenv("X11SHIM_LOG")) : 0;
    if (!dbg) return;
    va_list a; va_start(a, f); fprintf(stderr, "[x11shim] "); vfprintf(stderr, f, a); fputc('\n', stderr); va_end(a);
}

static pthread_mutex_t mtx = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
#define LOCK pthread_mutex_lock(&mtx)
#define UNLOCK pthread_mutex_unlock(&mtx)

static int SW = 720, SH = 480;            // screen = panel
static _XPrivDisplay dpy0;
static Screen scr0;
static Visual vis0;
static Depth depth0;
static SDL_Window *win;
static Window xwin;                       // the game's (single) window id
static Window next_id = 0x200000;
static Atom wm_delete;

// ---------------------------------------------------------------- fb0 + SDL
static void fix_fb0(void) {
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) return;
    struct fb_var_screeninfo v;
    if (!ioctl(fd, FBIOGET_VSCREENINFO, &v)) {
        // the menu can leave fb0 at 1280x1024@16; Mali EGL then refuses the window surface
        if (v.xres != (unsigned)SW || v.yres != (unsigned)SH || v.bits_per_pixel != 32 || v.yres_virtual < v.yres * 2) {
            v.xres = v.xres_virtual = SW; v.yres = SH; v.yres_virtual = SH * 2; v.xoffset = v.yoffset = 0;
            v.bits_per_pixel = 32;
            v.red.offset = 16; v.green.offset = 8; v.blue.offset = 0; v.transp.offset = 24;
            v.red.length = v.green.length = v.blue.length = v.transp.length = 8;
            if (ioctl(fd, FBIOPUT_VSCREENINFO, &v)) L("fb0 set failed");
        }
    }
    close(fd);
}

static SDL_GameController *pad;
static int sdl_ready;
static void sdl_up(void) {
    if (sdl_ready) return;
    sdl_ready = 1;
    fix_fb0();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) fprintf(stderr, "[x11shim] SDL_Init: %s\n", SDL_GetError());
    if (getenv("X11SHIM_PADKEYS") && atoi(getenv("X11SHIM_PADKEYS"))) {
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
        for (int i = 0; i < SDL_NumJoysticks(); i++)
            if (SDL_IsGameController(i) && (pad = SDL_GameControllerOpen(i))) { L("pad %s", SDL_GameControllerName(pad)); break; }
    }
    SDL_ShowCursor(0);
}
static void make_window(void) {
    if (win) return;
    sdl_up();
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    // the game blends with destination alpha (lighting): the window surface needs an alpha channel
    int asz = getenv("X11SHIM_ALPHA") ? atoi(getenv("X11SHIM_ALPHA")) : 0;
    if (asz) { SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8); SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8); SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8); SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, asz); }
    win = SDL_CreateWindow("Hotline Miami", 0, 0, SW, SH, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
    if (!win) {   // retry without depth/stencil
        SDL_GL_ResetAttributes();
        win = SDL_CreateWindow("Hotline Miami", 0, 0, SW, SH, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
    }
    if (!win) fprintf(stderr, "[x11shim] SDL_CreateWindow: %s\n", SDL_GetError());
    else { int w, h; SDL_GL_GetDrawableSize(win, &w, &h); if (w > 0) { SW = w; SH = h; } L("window %dx%d", SW, SH); }
}

// ---------------------------------------------------------------- event queue
#define QN 256
static XEvent q[QN];
static int qh, qt;
static void push(XEvent *e) { int n = (qt + 1) % QN; if (n == qh) return; q[qt] = *e; qt = n; }

// keyboard: X keycode = evdev code + 8 (as Xorg evdev)
static unsigned char keymap[32];
static void setkey(int kc, int down) {
    if (kc < 8 || kc > 255) return;
    int was = (keymap[kc >> 3] >> (kc & 7)) & 1;
    if (was == down) return;
    if (down) keymap[kc >> 3] |= 1 << (kc & 7); else keymap[kc >> 3] &= ~(1 << (kc & 7));
    XEvent e; memset(&e, 0, sizeof e);
    e.xkey.type = down ? KeyPress : KeyRelease;
    e.xkey.display = (Display *)dpy0; e.xkey.window = xwin; e.xkey.root = scr0.root;
    e.xkey.time = SDL_GetTicks(); e.xkey.keycode = kc; e.xkey.same_screen = True;
    push(&e);
    L("key %d %s", kc, down ? "down" : "up");
}

// keysym <-> evdev code
static const struct { KeySym ks; int code; } ktab[] = {
    { XK_Escape, 1 }, { XK_1, 2 }, { XK_2, 3 }, { XK_3, 4 }, { XK_4, 5 }, { XK_5, 6 }, { XK_6, 7 }, { XK_7, 8 },
    { XK_8, 9 }, { XK_9, 10 }, { XK_0, 11 }, { XK_minus, 12 }, { XK_equal, 13 }, { XK_BackSpace, 14 }, { XK_Tab, 15 },
    { XK_q, 16 }, { XK_w, 17 }, { XK_e, 18 }, { XK_r, 19 }, { XK_t, 20 }, { XK_y, 21 }, { XK_u, 22 }, { XK_i, 23 },
    { XK_o, 24 }, { XK_p, 25 }, { XK_bracketleft, 26 }, { XK_bracketright, 27 }, { XK_Return, 28 }, { XK_Control_L, 29 },
    { XK_a, 30 }, { XK_s, 31 }, { XK_d, 32 }, { XK_f, 33 }, { XK_g, 34 }, { XK_h, 35 }, { XK_j, 36 }, { XK_k, 37 },
    { XK_l, 38 }, { XK_semicolon, 39 }, { XK_apostrophe, 40 }, { XK_grave, 41 }, { XK_Shift_L, 42 }, { XK_backslash, 43 },
    { XK_z, 44 }, { XK_x, 45 }, { XK_c, 46 }, { XK_v, 47 }, { XK_b, 48 }, { XK_n, 49 }, { XK_m, 50 }, { XK_comma, 51 },
    { XK_period, 52 }, { XK_slash, 53 }, { XK_Shift_R, 54 }, { XK_KP_Multiply, 55 }, { XK_Alt_L, 56 }, { XK_space, 57 },
    { XK_Caps_Lock, 58 }, { XK_F1, 59 }, { XK_F2, 60 }, { XK_F3, 61 }, { XK_F4, 62 }, { XK_F5, 63 }, { XK_F6, 64 },
    { XK_F7, 65 }, { XK_F8, 66 }, { XK_F9, 67 }, { XK_F10, 68 }, { XK_F11, 87 }, { XK_F12, 88 },
    { XK_KP_Enter, 96 }, { XK_Control_R, 97 }, { XK_Alt_R, 100 }, { XK_Home, 102 }, { XK_Up, 103 }, { XK_Prior, 104 },
    { XK_Left, 105 }, { XK_Right, 106 }, { XK_End, 107 }, { XK_Down, 108 }, { XK_Next, 109 }, { XK_Insert, 110 },
    { XK_Delete, 111 }, { XK_Pause, 119 }, { XK_Super_L, 125 },
    { XK_A, 30 }, { XK_B, 48 }, { XK_C, 46 }, { XK_D, 32 }, { XK_E, 18 }, { XK_F, 33 }, { XK_G, 34 }, { XK_H, 35 },
    { XK_I, 23 }, { XK_J, 36 }, { XK_K, 37 }, { XK_L, 38 }, { XK_M, 50 }, { XK_N, 49 }, { XK_O, 24 }, { XK_P, 25 },
    { XK_Q, 16 }, { XK_R, 19 }, { XK_S, 31 }, { XK_T, 20 }, { XK_U, 22 }, { XK_V, 47 }, { XK_W, 17 }, { XK_X, 45 },
    { XK_Y, 21 }, { XK_Z, 44 },
};
static int ks2code(KeySym ks) {
    for (unsigned i = 0; i < sizeof ktab / sizeof *ktab; i++) if (ktab[i].ks == ks) return ktab[i].code;
    return -1;
}
// SDL scancode -> evdev code (subset matching ktab)
static int sc2code(SDL_Scancode s) {
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) {
        static const unsigned char az[26] = { 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44 };
        return az[s - SDL_SCANCODE_A];
    }
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_0) return 2 + (s - SDL_SCANCODE_1);
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10) return 59 + (s - SDL_SCANCODE_F1);
    switch (s) {
    case SDL_SCANCODE_RETURN: return 28; case SDL_SCANCODE_ESCAPE: return 1; case SDL_SCANCODE_BACKSPACE: return 14;
    case SDL_SCANCODE_TAB: return 15; case SDL_SCANCODE_SPACE: return 57; case SDL_SCANCODE_MINUS: return 12;
    case SDL_SCANCODE_EQUALS: return 13; case SDL_SCANCODE_LEFTBRACKET: return 26; case SDL_SCANCODE_RIGHTBRACKET: return 27;
    case SDL_SCANCODE_BACKSLASH: return 43; case SDL_SCANCODE_SEMICOLON: return 39; case SDL_SCANCODE_APOSTROPHE: return 40;
    case SDL_SCANCODE_GRAVE: return 41; case SDL_SCANCODE_COMMA: return 51; case SDL_SCANCODE_PERIOD: return 52;
    case SDL_SCANCODE_SLASH: return 53; case SDL_SCANCODE_F11: return 87; case SDL_SCANCODE_F12: return 88;
    case SDL_SCANCODE_PAUSE: return 119; case SDL_SCANCODE_INSERT: return 110; case SDL_SCANCODE_HOME: return 102;
    case SDL_SCANCODE_PAGEUP: return 104; case SDL_SCANCODE_DELETE: return 111; case SDL_SCANCODE_END: return 107;
    case SDL_SCANCODE_PAGEDOWN: return 109; case SDL_SCANCODE_RIGHT: return 106; case SDL_SCANCODE_LEFT: return 105;
    case SDL_SCANCODE_DOWN: return 108; case SDL_SCANCODE_UP: return 103; case SDL_SCANCODE_KP_ENTER: return 96;
    case SDL_SCANCODE_LCTRL: return 29; case SDL_SCANCODE_LSHIFT: return 42; case SDL_SCANCODE_LALT: return 56;
    case SDL_SCANCODE_RCTRL: return 97; case SDL_SCANCODE_RSHIFT: return 54; case SDL_SCANCODE_RALT: return 100;
    default: return -1;
    }
}

// optional pad -> keys (only if the game's own joystick support is not used)
static int pad_btn_code(int b) {
    switch (b) {
    case SDL_CONTROLLER_BUTTON_A: return 28;          // Return
    case SDL_CONTROLLER_BUTTON_B: return 1;           // Escape
    case SDL_CONTROLLER_BUTTON_X: return 57;          // space
    case SDL_CONTROLLER_BUTTON_Y: return 42;          // shift
    case SDL_CONTROLLER_BUTTON_START: return 1;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return 17;    // W
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return 31;  // S
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return 30;  // A
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return 32; // D
    default: return -1;
    }
}

static void pump(void) {
    if (!sdl_ready) return;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_KEYDOWN: case SDL_KEYUP:
            if (!e.key.repeat) { int c = sc2code(e.key.keysym.scancode); if (c > 0) setkey(c + 8, e.type == SDL_KEYDOWN); }
            break;
        case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP: {
            int c = pad_btn_code(e.cbutton.button);
            if (c > 0) setkey(c + 8, e.type == SDL_CONTROLLERBUTTONDOWN);
            break; }
        case SDL_QUIT:
            if (wm_delete && xwin) {
                XEvent x; memset(&x, 0, sizeof x);
                x.xclient.type = ClientMessage; x.xclient.display = (Display *)dpy0; x.xclient.window = xwin;
                x.xclient.format = 32; x.xclient.data.l[0] = wm_delete; push(&x);
            }
            break;
        }
    }
}

// ---------------------------------------------------------------- Xlib
EXP Status XInitThreads(void) { return 1; }

EXP Display *XOpenDisplay(const char *name) {
    LOCK;
    L("XOpenDisplay(%s)", name ? name : "");
    if (!dpy0) {
        fix_fb0();
        // box86 reads private Xlibint fields (free_funcs, lock_fns, ...) past the public struct: keep them NULL
        dpy0 = calloc(1, 8192);
        vis0.visualid = 0x21; vis0.class = TrueColor; vis0.red_mask = 0xff0000; vis0.green_mask = 0xff00;
        vis0.blue_mask = 0xff; vis0.bits_per_rgb = 8; vis0.map_entries = 256;
        depth0.depth = 24; depth0.nvisuals = 1; depth0.visuals = &vis0;
        scr0.display = (Display *)dpy0; scr0.root = 0x100; scr0.width = SW; scr0.height = SH;
        scr0.mwidth = SW * 254 / 960; scr0.mheight = SH * 254 / 960;   // ~96 dpi
        scr0.ndepths = 1; scr0.depths = &depth0; scr0.root_depth = 24; scr0.root_visual = &vis0;
        scr0.cmap = 0x101; scr0.white_pixel = 0xffffff; scr0.black_pixel = 0;
        scr0.max_maps = scr0.min_maps = 1; scr0.backing_store = NotUseful; scr0.root_input_mask = 0;
        dpy0->fd = -1; dpy0->proto_major_version = 11; dpy0->vendor = "x11shim"; dpy0->release = 1;
        dpy0->display_name = "x11shim:0"; dpy0->default_screen = 0; dpy0->nscreens = 1; dpy0->screens = &scr0;
        dpy0->bitmap_unit = 32; dpy0->bitmap_pad = 32; dpy0->byte_order = LSBFirst; dpy0->bitmap_bit_order = LSBFirst;
        dpy0->min_keycode = 8; dpy0->max_keycode = 255; dpy0->qlen = 0;
    }
    UNLOCK;
    return (Display *)dpy0;
}
EXP int XCloseDisplay(Display *d) { L("XCloseDisplay"); return 0; }
EXP XIOErrorHandler XSetIOErrorHandler(XIOErrorHandler h) { return NULL; }
EXP int XFlush(Display *d) { return 1; }
EXP int XFree(void *p) { free(p); return 1; }

EXP Atom XInternAtom(Display *d, const char *name, Bool only_if_exists) {
    static char *names[256]; static int n;
    LOCK;
    Atom a = 0;
    for (int i = 0; i < n; i++) if (!strcmp(names[i], name)) { a = 100 + i; break; }
    if (!a && n < 256) { names[n] = strdup(name); a = 100 + n++; }
    if (!strcmp(name, "WM_DELETE_WINDOW")) wm_delete = a;
    UNLOCK;
    L("XInternAtom(%s) = %lu", name, a);
    return a;
}

EXP Colormap XCreateColormap(Display *d, Window w, Visual *v, int alloc) { return 0x101; }

EXP Window XCreateWindow(Display *d, Window parent, int x, int y, unsigned int w, unsigned int h, unsigned int bw,
                         int depth, unsigned int cls, Visual *v, unsigned long mask, XSetWindowAttributes *attr) {
    LOCK;
    Window id = next_id++;
    if (!xwin) xwin = id;
    L("XCreateWindow(%d,%d %ux%u depth %d) = 0x%lx", x, y, w, h, depth, id);
    UNLOCK;
    return id;
}
EXP int XDestroyWindow(Display *d, Window w) { L("XDestroyWindow(0x%lx)", w); return 1; }

static void push_struct_events(Window w) {
    XEvent e; memset(&e, 0, sizeof e);
    e.xconfigure.type = ConfigureNotify; e.xconfigure.display = (Display *)dpy0; e.xconfigure.event = w;
    e.xconfigure.window = w; e.xconfigure.width = SW; e.xconfigure.height = SH; push(&e);
    memset(&e, 0, sizeof e);
    e.xmap.type = MapNotify; e.xmap.display = (Display *)dpy0; e.xmap.event = w; e.xmap.window = w; push(&e);
    memset(&e, 0, sizeof e);
    e.xvisibility.type = VisibilityNotify; e.xvisibility.display = (Display *)dpy0; e.xvisibility.window = w;
    e.xvisibility.state = VisibilityUnobscured; push(&e);
    memset(&e, 0, sizeof e);
    e.xexpose.type = Expose; e.xexpose.display = (Display *)dpy0; e.xexpose.window = w;
    e.xexpose.width = SW; e.xexpose.height = SH; push(&e);
    memset(&e, 0, sizeof e);
    e.xfocus.type = FocusIn; e.xfocus.display = (Display *)dpy0; e.xfocus.window = w; e.xfocus.mode = NotifyNormal;
    e.xfocus.detail = NotifyAncestor; push(&e);
}
EXP int XMapWindow(Display *d, Window w) {
    LOCK; L("XMapWindow(0x%lx)", w); make_window(); push_struct_events(w); UNLOCK; return 1;
}
EXP int XMoveResizeWindow(Display *d, Window w, int x, int y, unsigned int ww, unsigned int hh) {
    LOCK; L("XMoveResizeWindow(%d,%d %ux%u)", x, y, ww, hh);
    XEvent e; memset(&e, 0, sizeof e);
    e.xconfigure.type = ConfigureNotify; e.xconfigure.display = (Display *)dpy0; e.xconfigure.event = w;
    e.xconfigure.window = w; e.xconfigure.width = SW; e.xconfigure.height = SH; push(&e);
    UNLOCK; return 1;
}
EXP Status XGetWindowAttributes(Display *d, Window w, XWindowAttributes *a) {
    memset(a, 0, sizeof *a);
    a->width = SW; a->height = SH; a->depth = 24; a->visual = &vis0; a->root = scr0.root; a->class = InputOutput;
    a->map_state = IsViewable; a->colormap = 0x101; a->screen = &scr0;
    return 1;
}
EXP int XSetStandardProperties(Display *d, Window w, const char *wn, const char *in, Pixmap p, char **argv, int argc, XSizeHints *h) { return 1; }
EXP Status XSetWMProtocols(Display *d, Window w, Atom *p, int n) { return 1; }
EXP void XSetWMSizeHints(Display *d, Window w, XSizeHints *h, Atom prop) { }
EXP int XSetInputFocus(Display *d, Window w, int revert, Time t) { return 1; }
EXP Status XSendEvent(Display *d, Window w, Bool prop, long mask, XEvent *e) { L("XSendEvent type %d", e ? e->type : -1); return 1; }

EXP Pixmap XCreateBitmapFromData(Display *d, Drawable dr, const char *data, unsigned int w, unsigned int h) { return next_id++; }
EXP Cursor XCreatePixmapCursor(Display *d, Pixmap s, Pixmap m, XColor *fg, XColor *bg, unsigned int x, unsigned int y) { return next_id++; }
EXP int XDefineCursor(Display *d, Window w, Cursor c) { return 1; }
EXP int XUndefineCursor(Display *d, Window w) { return 1; }
EXP int XFreeCursor(Display *d, Cursor c) { return 1; }
EXP int XGrabPointer(Display *d, Window w, Bool oe, unsigned int mask, int pm, int km, Window confine, Cursor c, Time t) { return GrabSuccess; }
EXP int XUngrabPointer(Display *d, Time t) { return 1; }
EXP Bool XQueryPointer(Display *d, Window w, Window *root, Window *child, int *rx, int *ry, int *wx, int *wy, unsigned int *mask) {
    if (root) *root = scr0.root; if (child) *child = 0;
    if (rx) *rx = SW / 2; if (ry) *ry = SH / 2; if (wx) *wx = SW / 2; if (wy) *wy = SH / 2; if (mask) *mask = 0;
    return True;
}

EXP KeyCode XKeysymToKeycode(Display *d, KeySym ks) {
    int c = ks2code(ks);
    L("XKeysymToKeycode(0x%lx) = %d", ks, c > 0 ? c + 8 : 0);
    return c > 0 ? c + 8 : 0;
}
EXP int XQueryKeymap(Display *d, char keys[32]) {
    LOCK; pump(); memcpy(keys, keymap, 32); UNLOCK; return 1;
}
EXP int XPending(Display *d) {
    LOCK; pump(); int n = (qt - qh + QN) % QN; UNLOCK; return n;
}
EXP int XNextEvent(Display *d, XEvent *ev) {
    for (;;) {
        LOCK; pump();
        if (qh != qt) { *ev = q[qh]; qh = (qh + 1) % QN; UNLOCK; return 0; }
        UNLOCK;
        SDL_Delay(2);
    }
}

// ---------------------------------------------------------------- GLX (re-exported by hm1gl.so)
static void *gl4es;
static void *(*gl4es_gpa)(const GLubyte *);
static void gl4es_up(void) {
    if (gl4es_gpa) return;
    const char *p = getenv("SDL_VIDEO_GL_DRIVER");
    gl4es = dlopen(p ? p : "libGL.so.1", RTLD_LAZY | RTLD_GLOBAL);
    if (gl4es) gl4es_gpa = dlsym(gl4es, "glXGetProcAddress");
    if (!gl4es_gpa) fprintf(stderr, "[x11shim] no gl4es glXGetProcAddress (%s)\n", dlerror());
}

EXP XVisualInfo *glXChooseVisual(Display *d, int screen, int *attr) {
    for (int i = 0; attr && attr[i] != None && i < 64; i++) L("glXChooseVisual attr %d", attr[i]);
    XVisualInfo *vi = calloc(1, sizeof *vi);
    vi->visual = &vis0; vi->visualid = vis0.visualid; vi->screen = 0; vi->depth = 24; vi->class = TrueColor;
    vi->red_mask = vis0.red_mask; vi->green_mask = vis0.green_mask; vi->blue_mask = vis0.blue_mask;
    vi->colormap_size = 256; vi->bits_per_rgb = 8;
    return vi;
}
EXP GLXContext glXCreateContext(Display *d, XVisualInfo *vi, GLXContext share, Bool direct) {
    LOCK;
    make_window();
    gl4es_up();
    SDL_GLContext c = win ? SDL_GL_CreateContext(win) : NULL;
    if (!c) fprintf(stderr, "[x11shim] SDL_GL_CreateContext: %s\n", SDL_GetError());
    L("glXCreateContext = %p", c);
    UNLOCK;
    return (GLXContext)c;
}
EXP void glXDestroyContext(Display *d, GLXContext c) { L("glXDestroyContext"); }
EXP Bool glXMakeCurrent(Display *d, GLXDrawable dr, GLXContext c) {
    int r = SDL_GL_MakeCurrent(c ? win : NULL, (SDL_GLContext)c);
    { static int n; if (dbg && n++ < 40) fprintf(stderr, "[x11shim] glXMakeCurrent tid %ld ctx %p = %d\n", (long)syscall(224), c, r); }
    if (c && getenv("X11SHIM_VSYNC")) SDL_GL_SetSwapInterval(atoi(getenv("X11SHIM_VSYNC")));
    return r == 0;
}
EXP void glXSwapBuffers(Display *d, GLXDrawable dr) {
    static unsigned frames; static Uint32 t0;
    if (win) SDL_GL_SwapWindow(win);
    frames++;
    Uint32 t = SDL_GetTicks();
    if (!t0) t0 = t;
    if (t - t0 >= 5000) { fprintf(stderr, "[x11shim] %.1f fps\n", frames * 1000.0 / (t - t0)); frames = 0; t0 = t; }
}
EXP __GLXextFuncPtr glXGetProcAddress(const GLubyte *n) {
    gl4es_up();
    // hm1gl.so (glspy) overrides win here too, so its diagnostics see extension calls
    static void *spy; if (!spy) spy = dlopen("hm1gl.so", RTLD_LAZY | RTLD_NOLOAD);
    void *f = spy ? dlsym(spy, (const char *)n) : NULL;
    if (f && f == dlsym(gl4es, (const char *)n)) f = NULL;   // only take functions glspy itself defines
    if (!f) f = gl4es_gpa ? gl4es_gpa(n) : NULL;
    if (dbg > 1) L("glXGetProcAddress(%s) = %p", n, f);
    return (__GLXextFuncPtr)f;
}
EXP __GLXextFuncPtr glXGetProcAddressARB(const GLubyte *n) { return glXGetProcAddress(n); }
// libCgGL checks for a current context/display before it accepts any profile
EXP GLXContext glXGetCurrentContext(void) { return (GLXContext)SDL_GL_GetCurrentContext(); }
EXP Display *glXGetCurrentDisplay(void) { return SDL_GL_GetCurrentContext() ? (Display *)dpy0 : NULL; }
EXP GLXDrawable glXGetCurrentDrawable(void) { return SDL_GL_GetCurrentContext() ? xwin : 0; }
EXP Bool glXQueryVersion(Display *d, int *maj, int *min) { if (maj) *maj = 1; if (min) *min = 4; return True; }
EXP const char *glXQueryExtensionsString(Display *d, int screen) { return "GLX_ARB_get_proc_address"; }
EXP Bool glXIsDirect(Display *d, GLXContext c) { return True; }
