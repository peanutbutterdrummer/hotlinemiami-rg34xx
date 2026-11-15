// glspy (part of hm1gl.so): box86 resolves libGL symbols with dlsym on hm1gl.so, so anything defined
// here wins over gl4es. HM1GL_LOG=1 logs shader/program creation and compile errors (Cg -> GL path).
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <GL/gl.h>
#include <GL/glext.h>

static int lg = -1;
static int on(void) { if (lg < 0) lg = getenv("HM1GL_LOG") ? atoi(getenv("HM1GL_LOG")) : 0; return lg; }
#define REAL(ret, name, ...) static ret (*r_##name)(__VA_ARGS__); if (!r_##name) r_##name = dlsym(RTLD_NEXT, #name)

const GLubyte *glGetString(GLenum e) {
    REAL(const GLubyte *, glGetString, GLenum);
    const GLubyte *s = r_glGetString(e);
    if (on()) fprintf(stderr, "[glspy] glGetString(0x%x) = %s\n", e, s ? (const char *)s : "(null)");
    // gl4es advertises GL_EXT_direct_state_access but implements only part of it; libCgGL then takes DSA paths
    // that misbehave. Hide it (HM1GL_DSA=1 keeps it) so Cg uses plain bind+set calls.
    if (e == GL_EXTENSIONS && s && (getenv("HM1GL_DSA") && !atoi(getenv("HM1GL_DSA")))) {
        static char *ext;
        if (!ext) {
            ext = strdup((const char *)s);
            const char *tok = "GL_EXT_direct_state_access";
            char *p = strstr(ext, tok);
            if (p) { size_t tl = strlen(tok); if (p[tl] == ' ') tl++; memmove(p, p + tl, strlen(p + tl) + 1); }
        }
        return (const GLubyte *)ext;
    }
    return s;
}

void glProgramStringARB(GLenum target, GLenum fmt, GLsizei len, const void *str) {
    REAL(void, glProgramStringARB, GLenum, GLenum, GLsizei, const void *);
    r_glProgramStringARB(target, fmt, len, str);
    if (on()) {
        REAL(const GLubyte *, glGetString, GLenum);
        GLint pos = -1; glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &pos);
        REAL(void, glGetProgramivARB, GLenum, GLenum, GLint *); GLint pid = -1; r_glGetProgramivARB(target, 0x8677, &pid);
        fprintf(stderr, "[glspy] glProgramStringARB(0x%x, %d chars) prog id %d errpos %d %s\n", target, len, pid, pos,
                pos >= 0 ? (const char *)r_glGetString(GL_PROGRAM_ERROR_STRING_ARB) : "");
        if (pos >= 0 || on() > 1) fprintf(stderr, "%.*s\n", len, (const char *)str);
    }
}

static void shader_log(GLuint s, const char *what) {
    REAL(void, glGetShaderiv, GLuint, GLenum, GLint *);
    REAL(void, glGetShaderInfoLog, GLuint, GLsizei, GLsizei *, char *);
    GLint ok = 1; r_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    char buf[2048] = ""; r_glGetShaderInfoLog(s, sizeof buf, NULL, buf);
    fprintf(stderr, "[glspy] %s(%u) ok=%d %s\n", what, s, ok, buf);
}
void glCompileShader(GLuint s) {
    REAL(void, glCompileShader, GLuint);
    r_glCompileShader(s);
    if (on()) shader_log(s, "glCompileShader");
}
void glCompileShaderARB(GLuint s) {
    REAL(void, glCompileShaderARB, GLuint);
    r_glCompileShaderARB(s);
    if (on()) shader_log(s, "glCompileShaderARB");
}
void glShaderSource(GLuint s, GLsizei n, const char *const *str, const GLint *len) {
    REAL(void, glShaderSource, GLuint, GLsizei, const char *const *, const GLint *);
    if (on() > 1) for (int i = 0; i < n; i++) fprintf(stderr, "[glspy] src %u:\n%s\n", s, str[i]);
    r_glShaderSource(s, n, str, len);
}
void glShaderSourceARB(GLuint s, GLsizei n, const char *const *str, const GLint *len) {
    REAL(void, glShaderSourceARB, GLuint, GLsizei, const char *const *, const GLint *);
    if (on() > 1) for (int i = 0; i < n; i++) fprintf(stderr, "[glspy] src %u:\n%s\n", s, str[i]);
    r_glShaderSourceARB(s, n, str, len);
}
static void prog_log(GLuint p, const char *what) {
    REAL(void, glGetProgramiv, GLuint, GLenum, GLint *);
    REAL(void, glGetProgramInfoLog, GLuint, GLsizei, GLsizei *, char *);
    GLint ok = 1; r_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    char buf[2048] = ""; r_glGetProgramInfoLog(p, sizeof buf, NULL, buf);
    fprintf(stderr, "[glspy] %s(%u) ok=%d %s\n", what, p, ok, buf);
}
void glLinkProgram(GLuint p) {
    REAL(void, glLinkProgram, GLuint);
    r_glLinkProgram(p);
    if (on()) prog_log(p, "glLinkProgram");
}
void glLinkProgramARB(GLuint p) {
    REAL(void, glLinkProgramARB, GLuint);
    r_glLinkProgramARB(p);
    if (on()) prog_log(p, "glLinkProgramARB");
}

// ---- render-to-texture diagnostics (HM1GL_LOG>=1): first N calls of each
static int limn = -1;
static int lim(int *n) { if (limn < 0) limn = getenv("HM1GL_LIM") ? atoi(getenv("HM1GL_LIM")) : 60; return on() && (*n)++ < limn; }
#define FWD(ret, name, sig, args, fmt, ...) \
ret name sig { static int n; REAL(ret, name, __VA_ARGS__); if (lim(&n)) fprintf(stderr, "[glspy] " #name fmt "\n", FWDARGS_##name); return r_##name args; }
#define VFWD(name, sig, args, fmt, ...) \
void name sig { static int n; REAL(void, name, __VA_ARGS__); if (lim(&n)) fprintf(stderr, "[glspy] " #name fmt "\n", FWDARGS_##name); r_##name args; }

#define FWDARGS_glBindFramebuffer t, f
#define FWDARGS_glBindFramebufferEXT t, f
VFWD(glBindFramebuffer, (GLenum t, GLuint f), (t, f), "(0x%x, %u)", GLenum, GLuint)
static GLuint cur_fbo; GLuint cur_att;

// Mali workarounds for stale render-to-texture content:
//  HM1GL_FBOFINISH=1: glFinish when leaving an offscreen FBO (Mali can reorder/drop offscreen work)
//  HM1GL_FBOCLEAR=1 : full clear (scissor off, all channels) the first time a fresh texture is attached
static int envi(const char *n, int d) { const char *v = getenv(n); return v ? atoi(v) : d; }
void glBindFramebufferEXT(GLenum t, GLuint f) {
    static int n, fin = -1; REAL(void, glBindFramebufferEXT, GLenum, GLuint);
    if (fin < 0) fin = envi("HM1GL_FBOFINISH", 0);
    if (lim(&n)) fprintf(stderr, "[glspy] glBindFramebufferEXT(0x%x, %u)\n", t, f);
    if (fin && cur_fbo && f != cur_fbo) { REAL(void, glFinish, void); r_glFinish(); }
    { extern void trx(const char*,unsigned,unsigned,unsigned,unsigned); trx("BindFramebuffer", f, 0, 0, 0); }
    cur_fbo = f; r_glBindFramebufferEXT(t, f);
}
static unsigned char *attached_once; static size_t att_cap;
static int was_attached(GLuint tex, int set) {
    if (tex >= att_cap) { size_t nc = att_cap ? att_cap : 4096; while (nc <= tex) nc *= 2; unsigned char *p = realloc(attached_once, nc); if (!p) return 1; memset(p + att_cap, 0, nc - att_cap); attached_once = p; att_cap = nc; }
    int r = attached_once[tex]; if (set >= 0) attached_once[tex] = set; return r;
}
#define FWDARGS_glFramebufferTexture2D t, a, tt, tex, l
#define FWDARGS_glFramebufferTexture2DEXT t, a, tt, tex, l
VFWD(glFramebufferTexture2D, (GLenum t, GLenum a, GLenum tt, GLuint tex, GLint l), (t, a, tt, tex, l), "(0x%x, att 0x%x, 0x%x, tex %u, lvl %d)", GLenum, GLenum, GLenum, GLuint, GLint)
void glFramebufferTexture2DEXT(GLenum t, GLenum a, GLenum tt, GLuint tex, GLint l) {
    static int n, clr = -1; REAL(void, glFramebufferTexture2DEXT, GLenum, GLenum, GLenum, GLuint, GLint);
    if (clr < 0) clr = envi("HM1GL_FBOCLEAR", 1);
    if (lim(&n)) fprintf(stderr, "[glspy] glFramebufferTexture2DEXT(0x%x, att 0x%x, 0x%x, tex %u, lvl %d)\n", t, a, tt, tex, l);
    if (a == 0x8CE0) cur_att = tex;
    {   // HM1GL_TRACEWIN=N: after the Nth-second mark, attaching a texture outside the per-frame set opens a trace window
        extern int trace_left; static long t0; struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); if (!t0) t0 = ts.tv_sec;
        int tw = envi("HM1GL_TRACEWIN", 0);
        if (tw && ts.tv_sec - t0 >= tw && tex != 1 && tex != 71 && tex != 79 && tex != 80 && tex != 81) {
            if (trace_left <= 0) fprintf(stderr, "[trace] ===== window opened by attach of tex %u\n", tex);
            trace_left = 400;
        }
        if (trace_left > 0) { trace_left--; fprintf(stderr, "[trace] attach tex %u (fbo %u)\n", tex, cur_fbo); }
    }
    r_glFramebufferTexture2DEXT(t, a, tt, tex, l);
    if (clr && tex && a == 0x8CE0 && !was_attached(tex, 1)) {
        REAL(GLboolean, glIsEnabled, GLenum); REAL(void, glDisable, GLenum); REAL(void, glEnable, GLenum);
        REAL(void, glClear, GLbitfield); REAL(void, glClearColor, GLclampf, GLclampf, GLclampf, GLclampf);
        REAL(void, glColorMask, GLboolean, GLboolean, GLboolean, GLboolean); REAL(void, glGetFloatv, GLenum, GLfloat *);
        REAL(void, glGetBooleanv, GLenum, GLboolean *);
        GLboolean sc = r_glIsEnabled(0x0C11), cm[4]; GLfloat cc0[4];
        r_glGetBooleanv(0x0C23, cm); r_glGetFloatv(0x0C22, cc0);
        if (sc) r_glDisable(0x0C11);
        r_glColorMask(1, 1, 1, 1); r_glClearColor(0, 0, 0, 0); r_glClear(0x4000);
        r_glClearColor(cc0[0], cc0[1], cc0[2], cc0[3]); r_glColorMask(cm[0], cm[1], cm[2], cm[3]);
        if (sc) r_glEnable(0x0C11);
        static int nn; if (nn++ < 50) fprintf(stderr, "[glspy] FBOCLEAR: first attach of tex %u cleared\n", tex);
    }
}
#define FWDARGS_glFramebufferRenderbuffer t, a, rt, rb
#define FWDARGS_glFramebufferRenderbufferEXT t, a, rt, rb
VFWD(glFramebufferRenderbuffer, (GLenum t, GLenum a, GLenum rt, GLuint rb), (t, a, rt, rb), "(0x%x, att 0x%x, 0x%x, rb %u)", GLenum, GLenum, GLenum, GLuint)
VFWD(glFramebufferRenderbufferEXT, (GLenum t, GLenum a, GLenum rt, GLuint rb), (t, a, rt, rb), "(0x%x, att 0x%x, 0x%x, rb %u)", GLenum, GLenum, GLenum, GLuint)
#define FWDARGS_glRenderbufferStorage t, f, w, h
#define FWDARGS_glRenderbufferStorageEXT t, f, w, h
VFWD(glRenderbufferStorage, (GLenum t, GLenum f, GLsizei w, GLsizei h), (t, f, w, h), "(0x%x, fmt 0x%x, %dx%d)", GLenum, GLenum, GLsizei, GLsizei)
VFWD(glRenderbufferStorageEXT, (GLenum t, GLenum f, GLsizei w, GLsizei h), (t, f, w, h), "(0x%x, fmt 0x%x, %dx%d)", GLenum, GLenum, GLsizei, GLsizei)
#define FWDARGS_glCopyTexSubImage2D t, l, xo, yo, x, y, w, h
VFWD(glCopyTexSubImage2D, (GLenum t, GLint l, GLint xo, GLint yo, GLint x, GLint y, GLsizei w, GLsizei h), (t, l, xo, yo, x, y, w, h), "(0x%x, %d, %d,%d, %d,%d %dx%d)", GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei)
#define FWDARGS_glCopyTexImage2D t, l, f, x, y, w, h, b
VFWD(glCopyTexImage2D, (GLenum t, GLint l, GLenum f, GLint x, GLint y, GLsizei w, GLsizei h, GLint b), (t, l, f, x, y, w, h, b), "(0x%x, %d, fmt 0x%x, %d,%d %dx%d)", GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint)

GLenum glCheckFramebufferStatus(GLenum t) { static int n; REAL(GLenum, glCheckFramebufferStatus, GLenum); GLenum r = r_glCheckFramebufferStatus(t); if (lim(&n)) fprintf(stderr, "[glspy] glCheckFramebufferStatus = 0x%x\n", r); return r; }
GLenum glCheckFramebufferStatusEXT(GLenum t) { static int n; REAL(GLenum, glCheckFramebufferStatusEXT, GLenum); GLenum r = r_glCheckFramebufferStatusEXT(t); if (lim(&n)) fprintf(stderr, "[glspy] glCheckFramebufferStatusEXT = 0x%x\n", r); return r; }
void glCompressedTexImage2D(GLenum t, GLint l, GLenum f, GLsizei w, GLsizei h, GLint b, GLsizei sz, const void *d) {
    static int n; REAL(void, glCompressedTexImage2D, GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *);
    if (lim(&n)) fprintf(stderr, "[glspy] glCompressedTexImage2D(lvl %d, fmt 0x%x, %dx%d, %d bytes)\n", l, f, w, h, sz);
    r_glCompressedTexImage2D(t, l, f, w, h, b, sz, d);
}
void glCompressedTexImage2DARB(GLenum t, GLint l, GLenum f, GLsizei w, GLsizei h, GLint b, GLsizei sz, const void *d) {
    static int n; REAL(void, glCompressedTexImage2DARB, GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *);
    if (lim(&n)) fprintf(stderr, "[glspy] glCompressedTexImage2DARB(lvl %d, fmt 0x%x, %dx%d, %d bytes)\n", l, f, w, h, sz);
    r_glCompressedTexImage2DARB(t, l, f, w, h, b, sz, d);
}
void glPixelStorei(GLenum p, GLint v) {
    static int n; REAL(void, glPixelStorei, GLenum, GLint);
    if (lim(&n)) fprintf(stderr, "[glspy] glPixelStorei(0x%x, %d)\n", p, v);
    r_glPixelStorei(p, v);
}

// ---- GL_PIXEL_UNPACK_BUFFER emulation. The game streams every texture through an unpack PBO
// (glTexSubImage2D(..., NULL) = offset 0 of the bound PBO); gl4es' PBO path leaves garbage (speckled floors).
// We keep unpack buffers in plain memory here and hand gl4es real pointers. HM1GL_PBO=0 disables.
#define PUB 0x88EC
typedef struct { GLuint id; void *mem; GLsizeiptr size; } pbo_t;
// indexed by buffer id (gl4es hands out small sequential ids), grown on demand - the game keeps hundreds alive
static pbo_t *pbos;
static size_t pbocap;
static GLuint cur_unpack;
static int pbo_on = -1;
static int pboe(void) { if (pbo_on < 0) pbo_on = getenv("HM1GL_PBO") ? atoi(getenv("HM1GL_PBO")) : 1; return pbo_on; }
static pbo_t *pbo_get(GLuint id, int make) {
    if (!id) return NULL;
    if (id >= pbocap) {
        if (!make) return NULL;
        size_t nc = pbocap ? pbocap : 1024; while (nc <= id) nc *= 2;
        pbo_t *n = realloc(pbos, nc * sizeof *n);
        if (!n) { fprintf(stderr, "[glspy] PBO table alloc failed (%zu)\n", nc); return NULL; }
        memset(n + pbocap, 0, (nc - pbocap) * sizeof *n); pbos = n; pbocap = nc;
    }
    pbo_t *p = &pbos[id];
    if (p->id == id) return p;
    if (!make) return NULL;
    p->id = id; p->mem = NULL; p->size = 0; return p;
}
static int stat_up, stat_map;
#define BIND(name) void name(GLenum t, GLuint id) { \
    REAL(void, name, GLenum, GLuint); \
    if (t == PUB && pboe()) { cur_unpack = id; if (id) pbo_get(id, 1); return; } \
    r_##name(t, id); }
BIND(glBindBuffer)
BIND(glBindBufferARB)
#define BDATA(name) void name(GLenum t, GLsizeiptr sz, const void *d, GLenum u) { \
    REAL(void, name, GLenum, GLsizeiptr, const void *, GLenum); \
    if (t == PUB && pboe()) { pbo_t *p = pbo_get(cur_unpack, 1); if (!p) return; \
        if (p->size != sz) { free(p->mem); p->mem = calloc(1, sz ? sz : 1); p->size = sz; } \
        if (d) memcpy(p->mem, d, sz); return; } \
    r_##name(t, sz, d, u); }
BDATA(glBufferData)
BDATA(glBufferDataARB)
#define BSUB(name) void name(GLenum t, GLintptr o, GLsizeiptr sz, const void *d) { \
    REAL(void, name, GLenum, GLintptr, GLsizeiptr, const void *); \
    if (t == PUB && pboe()) { pbo_t *p = pbo_get(cur_unpack, 0); if (p && p->mem && o + sz <= p->size) memcpy((char *)p->mem + o, d, sz); return; } \
    r_##name(t, o, sz, d); }
BSUB(glBufferSubData)
BSUB(glBufferSubDataARB)
#define BMAP(name) void *name(GLenum t, GLenum a) { \
    REAL(void *, name, GLenum, GLenum); \
    if (t == PUB && pboe()) { pbo_t *p = pbo_get(cur_unpack, 0); stat_map++; return p ? p->mem : NULL; } \
    return r_##name(t, a); }
BMAP(glMapBuffer)
BMAP(glMapBufferARB)
void *glMapBufferRange(GLenum t, GLintptr o, GLsizeiptr l, GLbitfield a) {
    REAL(void *, glMapBufferRange, GLenum, GLintptr, GLsizeiptr, GLbitfield);
    { extern void trx(const char*,unsigned,unsigned,unsigned,unsigned); trx("MapBufferRange", t, (unsigned)o, (unsigned)l, a); }
    if (t == PUB && pboe()) { pbo_t *p = pbo_get(cur_unpack, 0); stat_map++; return p && p->mem ? (char *)p->mem + o : NULL; }
    return r_glMapBufferRange(t, o, l, a);
}
#define BUNMAP(name) GLboolean name(GLenum t) { \
    REAL(GLboolean, name, GLenum); \
    if (t == PUB && pboe()) return GL_TRUE; \
    return r_##name(t); }
BUNMAP(glUnmapBuffer)
BUNMAP(glUnmapBufferARB)
#define BDEL(name) void name(GLsizei n, const GLuint *ids) { \
    REAL(void, name, GLsizei, const GLuint *); \
    if (pboe()) for (int i = 0; i < n; i++) { pbo_t *p = pbo_get(ids[i], 0); if (p) { free(p->mem); p->mem = NULL; p->size = 0; p->id = 0; if (cur_unpack == ids[i]) cur_unpack = 0; } } \
    r_##name(n, ids); }
BDEL(glDeleteBuffers)
BDEL(glDeleteBuffersARB)

static const void *unpack_ptr(const void *d) {
    if (!pboe() || !cur_unpack) return d;
    pbo_t *p = pbo_get(cur_unpack, 0);
    stat_up++;
    if (!p || !p->mem || (uintptr_t)d >= (uintptr_t)p->size) {
        static int miss; if (miss++ < 20) fprintf(stderr, "[glspy] PBO miss: buffer %u (%s, size %ld, offset %lu)\n", cur_unpack, p ? "no data" : "unknown", p ? (long)p->size : -1L, (unsigned long)(uintptr_t)d);
        return d;
    }
    return (const char *)p->mem + (uintptr_t)d;
}
void glTexImage2D(GLenum t, GLint l, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void *d) {
    static int n; REAL(void, glTexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
    if (lim(&n)) fprintf(stderr, "[glspy] glTexImage2D(0x%x, lvl %d, ifmt 0x%x, %dx%d, fmt 0x%x, type 0x%x, %p, pbo %u)\n", t, l, ifmt, w, h, f, ty, d, cur_unpack);
    r_glTexImage2D(t, l, ifmt, w, h, b, f, ty, (d || cur_unpack) ? unpack_ptr(d) : NULL);
}
void glTexSubImage2D(GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum ty, const void *d) {
    static int n; REAL(void, glTexSubImage2D, GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
    {   // uploads issued on a thread that does not hold the EGL context are silently lost
        static void *(*egc)(void); if (!egc) egc = dlsym(RTLD_DEFAULT, "eglGetCurrentContext");
        static int nc, tot; tot++;
        if (egc && !egc() && nc++ < 50) fprintf(stderr, "[glspy] UPLOAD WITHOUT CONTEXT tid %ld %dx%d (%d/%d)\n", (long)syscall(224), w, h, nc, tot);
    }
    const void *q = unpack_ptr(d);
    if (getenv("HM1GL_TEXLOG")) { GLint b = 0, u = 0; glGetIntegerv(0x8069, &b); glGetIntegerv(0x84E0, &u); unsigned sum = 0; if (q) for (int k = 0; k < w * h; k += 997) sum += ((const unsigned char *)q)[k * (f == 0x1909 ? 1 : 4)]; fprintf(stderr, "[tex] sub tex %d unit 0x%x %dx%d fmt 0x%x data %s sum %u\n", b, u, w, h, f, q ? "ok" : "NULL", sum); }
    if (lim(&n)) fprintf(stderr, "[glspy] glTexSubImage2D(lvl %d, %d,%d %dx%d, fmt 0x%x, type 0x%x, %p -> %p, pbo %u, ups %d maps %d)\n", l, x, y, w, h, f, ty, d, q, cur_unpack, stat_up, stat_map);
    static int dumped; const char *dd = getenv("HM1GL_DUMP");
    int want = getenv("HM1GL_DUMPFMT") ? (int)strtol(getenv("HM1GL_DUMPFMT"), NULL, 0) : 0;   // 0 = L+RGBA
    if (dd && q && dumped < 60 && w >= 128 && w < 1920 && (want ? f == (GLenum)want : (f == 0x1909 || f == 0x1908))) {
        char fn[256]; snprintf(fn, sizeof fn, "%s/t%02d_%dx%d_%s.raw", dd, dumped++, w, h, f == 0x1909 ? "L" : "RGBA");
        FILE *o = fopen(fn, "wb"); if (o) { fwrite(q, 1, (size_t)w * h * (f == 0x1909 ? 1 : 4), o); fclose(o); }
    }
    // FIX: the game's 1024x1024 grey film-grain texture is meant as a faint overlay but ends up opaque here
    // (its per-draw alpha is lost somewhere in the Cg/gl4es path) and hides the level. Bake a low alpha into it.
    // HM1GL_NOISE_ALPHA = 0..255 (default 24, 255 = original). Needs the PBO emulation (data pointer).
    static int na = -1; if (na < 0) na = getenv("HM1GL_NOISE_ALPHA") ? atoi(getenv("HM1GL_NOISE_ALPHA")) : 24;
    if (q && na < 255 && w == 1024 && h == 1024 && f == 0x1908) {
        const unsigned char *s = q; int grey = 1;
        for (int k = 0; k < 4096 && grey; k += 37) { const unsigned char *p = s + 4 * (k * 97 % (512 * 1024)); if (p[3] && (p[0] != p[1] || p[1] != p[2])) grey = 0; }
        if (grey) {
            unsigned char *c = malloc((size_t)w * h * 4); memcpy(c, s, (size_t)w * h * 4);
            for (size_t k = 0; k < (size_t)w * h; k++) c[4 * k + 3] = c[4 * k + 3] * na / 255;
            fprintf(stderr, "[glspy] film grain texture alpha scaled to %d/255\n", na);
            r_glTexSubImage2D(t, l, x, y, w, h, f, ty, c); free(c); return;
        }
    }
    r_glTexSubImage2D(t, l, x, y, w, h, f, ty, q);
}

// ---- ARB program parameter diagnostics (HM1GL_LOG>=1)
static void plog(const char *fn, GLenum t, GLuint i, const float *v) {
    static int n; if (!on() || n++ > 400) return;
    fprintf(stderr, "[glspy] %s(0x%x, %u) = %.3f %.3f %.3f %.3f\n", fn, t, i, v[0], v[1], v[2], v[3]);
}
#define P4F(name) void name(GLenum t, GLuint i, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { \
    REAL(void, name, GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat); float v[4] = { x, y, z, w }; plog(#name, t, i, v); r_##name(t, i, x, y, z, w); }
#define P4FV(name) void name(GLenum t, GLuint i, const GLfloat *v) { \
    REAL(void, name, GLenum, GLuint, const GLfloat *); plog(#name, t, i, v); r_##name(t, i, v); }
P4F(glProgramLocalParameter4fARB)
P4FV(glProgramLocalParameter4fvARB)
P4F(glProgramEnvParameter4fARB)
P4FV(glProgramEnvParameter4fvARB)
void glProgramLocalParameters4fvEXT(GLenum t, GLuint i, GLsizei c, const GLfloat *v) {
    REAL(void, glProgramLocalParameters4fvEXT, GLenum, GLuint, GLsizei, const GLfloat *); plog("glProgramLocalParameters4fvEXT", t, i, v);
    if (r_glProgramLocalParameters4fvEXT) r_glProgramLocalParameters4fvEXT(t, i, c, v);
    else for (int k = 0; k < c; k++) glProgramLocalParameter4fvARB(t, i + k, v + 4 * k);
}

// ---- EXT_direct_state_access program parameters, which libCgGL uses to set every arbvp1/arbfp1 constant
// (colour, alpha, matrices). gl4es lacks them, so all constants stayed at their defaults (e.g. opaque overlays).
// Emulated as bind / set / restore on top of the ARB calls.
#ifndef GL_PROGRAM_BINDING_ARB
#define GL_PROGRAM_BINDING_ARB 0x8677
#endif
static void named_set(GLuint prog, GLenum target, GLuint idx, GLsizei cnt, const GLfloat *v) {
    REAL(void, glGetProgramivARB, GLenum, GLenum, GLint *);
    REAL(void, glBindProgramARB, GLenum, GLuint);
    REAL(void, glProgramLocalParameter4fvARB, GLenum, GLuint, const GLfloat *);
    GLint old = 0; r_glGetProgramivARB(target, GL_PROGRAM_BINDING_ARB, &old);
    if ((GLuint)old != prog) r_glBindProgramARB(target, prog);
    for (GLsizei k = 0; k < cnt; k++) r_glProgramLocalParameter4fvARB(target, idx + k, v + 4 * k);
    if ((GLuint)old != prog) r_glBindProgramARB(target, old);
    static int n; if (on() && (target == 0x8804 || on() > 2) && n++ < 3000) fprintf(stderr, "[glspy] named param prog %u tgt 0x%x idx %u x%d = %.3f %.3f %.3f %.3f (bound %d)\n", prog, target, idx, cnt, v[0], v[1], v[2], v[3], old);
}
void glNamedProgramLocalParameter4fvEXT(GLuint p, GLenum t, GLuint i, const GLfloat *v) { named_set(p, t, i, 1, v); }
void glNamedProgramLocalParameters4fvEXT(GLuint p, GLenum t, GLuint i, GLsizei c, const GLfloat *v) { named_set(p, t, i, c, v); }
void glNamedProgramLocalParameter4fEXT(GLuint p, GLenum t, GLuint i, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { GLfloat v[4] = { x, y, z, w }; named_set(p, t, i, 1, v); }
void glNamedProgramLocalParameterI4ivEXT(GLuint p, GLenum t, GLuint i, const GLint *iv) { GLfloat v[4] = { iv[0], iv[1], iv[2], iv[3] }; named_set(p, t, i, 1, v); }

// ---- blend state diagnostics (HM1GL_LOG>=1): distinct combinations only
static void blog(const char *fn, unsigned a, unsigned b, unsigned c, unsigned d) {
    static unsigned seen[64][5]; static int ns;
    if (!on()) return;
    for (int i = 0; i < ns; i++) if (seen[i][0] == (unsigned)(uintptr_t)fn && seen[i][1] == a && seen[i][2] == b && seen[i][3] == c && seen[i][4] == d) return;
    if (ns < 64) { seen[ns][0] = (unsigned)(uintptr_t)fn; seen[ns][1] = a; seen[ns][2] = b; seen[ns][3] = c; seen[ns][4] = d; ns++; }
    fprintf(stderr, "[glspy] %s(0x%x, 0x%x, 0x%x, 0x%x)\n", fn, a, b, c, d);
}
void glBlendFunc(GLenum s, GLenum d) { REAL(void, glBlendFunc, GLenum, GLenum); blog("glBlendFunc", s, d, 0, 0); r_glBlendFunc(s, d); }
void glBlendFuncSeparate(GLenum a, GLenum b, GLenum c, GLenum d) { REAL(void, glBlendFuncSeparate, GLenum, GLenum, GLenum, GLenum); blog("glBlendFuncSeparate", a, b, c, d); r_glBlendFuncSeparate(a, b, c, d); }
void glBlendFuncSeparateEXT(GLenum a, GLenum b, GLenum c, GLenum d) { REAL(void, glBlendFuncSeparateEXT, GLenum, GLenum, GLenum, GLenum); blog("glBlendFuncSeparateEXT", a, b, c, d); r_glBlendFuncSeparateEXT(a, b, c, d); }
void glBlendEquation(GLenum m) { REAL(void, glBlendEquation, GLenum); blog("glBlendEquation", m, 0, 0, 0); r_glBlendEquation(m); }
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { REAL(void, glColorMask, GLboolean, GLboolean, GLboolean, GLboolean); blog("glColorMask", r, g, b, a); r_glColorMask(r, g, b, a); }
void glAlphaFunc(GLenum f, GLclampf r) { REAL(void, glAlphaFunc, GLenum, GLclampf); blog("glAlphaFunc", f, (unsigned)(r * 1000), 0, 0); r_glAlphaFunc(f, r); }
// ---- vertex array diagnostics (distinct layouts)
void glVertexAttribPointer(GLuint i, GLint sz, GLenum t, GLboolean n, GLsizei st, const void *p) { REAL(void, glVertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void *); blog("glVertexAttribPointer", i, sz | (n << 8), t, st); r_glVertexAttribPointer(i, sz, t, n, st, p); }
void glVertexAttribPointerARB(GLuint i, GLint sz, GLenum t, GLboolean n, GLsizei st, const void *p) { REAL(void, glVertexAttribPointerARB, GLuint, GLint, GLenum, GLboolean, GLsizei, const void *); blog("glVertexAttribPointerARB", i, sz | (n << 8), t, st); r_glVertexAttribPointerARB(i, sz, t, n, st, p); }
void glTexCoordPointer(GLint sz, GLenum t, GLsizei st, const void *p) { REAL(void, glTexCoordPointer, GLint, GLenum, GLsizei, const void *); blog("glTexCoordPointer", sz, t, st, 0); r_glTexCoordPointer(sz, t, st, p); }
void glColorPointer(GLint sz, GLenum t, GLsizei st, const void *p) { REAL(void, glColorPointer, GLint, GLenum, GLsizei, const void *); blog("glColorPointer", sz, t, st, 0); r_glColorPointer(sz, t, st, p); }
void glVertexPointer(GLint sz, GLenum t, GLsizei st, const void *p) { REAL(void, glVertexPointer, GLint, GLenum, GLsizei, const void *); blog("glVertexPointer", sz, t, st, 0); r_glVertexPointer(sz, t, st, p); }
void glClientActiveTexture(GLenum t) { REAL(void, glClientActiveTexture, GLenum); blog("glClientActiveTexture", t, 0, 0, 0); r_glClientActiveTexture(t); }
void glClientActiveTextureARB(GLenum t) { REAL(void, glClientActiveTextureARB, GLenum); blog("glClientActiveTextureARB", t, 0, 0, 0); r_glClientActiveTextureARB(t); }
// ---- clear / scissor diagnostics
static float cc[4]; static int scis;
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { REAL(void, glClearColor, GLclampf, GLclampf, GLclampf, GLclampf); cc[0] = r; cc[1] = g; cc[2] = b; cc[3] = a; r_glClearColor(r, g, b, a); }
static void offlog(const char *fn, int a, int b, int c, int d);
void glClear(GLbitfield m) {
    REAL(void, glClear, GLbitfield);
    offlog("glClear", m, scis, 0, 0);
    { extern void trx(const char*,unsigned,unsigned,unsigned,unsigned); trx("Clear", m, scis, 0, 0); }
    blog("glClear(mask,fbo,scissor,rgba8888)", m, cur_fbo, scis, ((unsigned)(cc[0] * 255) << 24) | ((unsigned)(cc[1] * 255) << 16) | ((unsigned)(cc[2] * 255) << 8) | (unsigned)(cc[3] * 255));
    r_glClear(m);
}
void glEnable(GLenum c) { REAL(void, glEnable, GLenum); if (c == 0x0C11) scis = 1; blog("glEnable", c, 0, 0, 0); r_glEnable(c); }
void glDisable(GLenum c) { REAL(void, glDisable, GLenum); if (c == 0x0C11) scis = 0; blog("glDisable", c, 0, 0, 0); r_glDisable(c); }
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) { REAL(void, glScissor, GLint, GLint, GLsizei, GLsizei); blog("glScissor", x, y, w, h); r_glScissor(x, y, w, h); }

// ---- EXT_direct_state_access texture binds used by libCgGL (cgGLSetTextureParameter/EnableTextureParameter).
// Emulated with ActiveTexture + BindTexture + restore, instead of trusting gl4es' partial DSA.
#ifndef GL_ACTIVE_TEXTURE
#define GL_ACTIVE_TEXTURE 0x84E0
#endif
static int dsatex = -1;
static int dsat(void) { if (dsatex < 0) dsatex = getenv("HM1GL_DSATEX") ? atoi(getenv("HM1GL_DSATEX")) : 0; return dsatex; }
void glBindMultiTextureEXT(GLenum unit, GLenum target, GLuint tex) {
    REAL(void, glBindMultiTextureEXT, GLenum, GLenum, GLuint);
    static int n; if (lim(&n)) fprintf(stderr, "[glspy] glBindMultiTextureEXT(0x%x, 0x%x, %u)\n", unit, target, tex);
    { extern void set_lb(GLuint, const char *); set_lb(tex, "BindMulti"); }
    if (!dsat() && r_glBindMultiTextureEXT) { r_glBindMultiTextureEXT(unit, target, tex); return; }
    REAL(void, glActiveTexture, GLenum); REAL(void, glBindTexture, GLenum, GLuint); REAL(void, glGetIntegerv, GLenum, GLint *);
    GLint old = GL_TEXTURE0; r_glGetIntegerv(GL_ACTIVE_TEXTURE, &old);
    if ((GLenum)old != unit) r_glActiveTexture(unit);
    r_glBindTexture(target, tex);
    if ((GLenum)old != unit) r_glActiveTexture(old);
}
void glMultiTexParameteriEXT(GLenum unit, GLenum target, GLenum p, GLint v) {
    REAL(void, glMultiTexParameteriEXT, GLenum, GLenum, GLenum, GLint);
    if (!dsat() && r_glMultiTexParameteriEXT) { r_glMultiTexParameteriEXT(unit, target, p, v); return; }
    REAL(void, glActiveTexture, GLenum); REAL(void, glTexParameteri, GLenum, GLenum, GLint); REAL(void, glGetIntegerv, GLenum, GLint *);
    GLint old = GL_TEXTURE0; r_glGetIntegerv(GL_ACTIVE_TEXTURE, &old);
    if ((GLenum)old != unit) r_glActiveTexture(unit);
    r_glTexParameteri(target, p, v);
    if ((GLenum)old != unit) r_glActiveTexture(old);
}

// ---- texture object lifecycle diagnostics (HM1GL_TEXLOG=1)
static int texlog(void) { static int t = -1; if (t < 0) t = getenv("HM1GL_TEXLOG") ? atoi(getenv("HM1GL_TEXLOG")) : 0; return t; }
// HM1GL_FRESHTEX=1: never hand out a texture name that was used before (gl4es mishandles recycled names?)
void glGenTextures(GLsizei n, GLuint *ids) {
    REAL(void, glGenTextures, GLsizei, GLuint *);
    static int fresh = -1; if (fresh < 0) fresh = getenv("HM1GL_FRESHTEX") ? atoi(getenv("HM1GL_FRESHTEX")) : 0;
    static GLuint hi;
    if (fresh) {
        for (GLsizei i = 0; i < n; i++) {
            GLuint id; int tries = 0; do { r_glGenTextures(1, &id); } while (id <= hi && id && ++tries < 4096);   // skip names at or below the high-water mark
            hi = ids[i] = id;
        }
    } else r_glGenTextures(n, ids);
    if (texlog()) for (int i = 0; i < n; i++) fprintf(stderr, "[tex] gen %u\n", ids[i]); }
void glDeleteTextures(GLsizei n, const GLuint *ids) { REAL(void, glDeleteTextures, GLsizei, const GLuint *); for (int i = 0; i < n; i++) if (ids[i] < att_cap) attached_once[ids[i]] = 0; if (texlog()) for (int i = 0; i < n; i++) fprintf(stderr, "[tex] del %u\n", ids[i]); r_glDeleteTextures(n, ids); }

// ---- draws into offscreen surfaces (HM1GL_DRAWLOG=1): which texture/program/viewport each draw uses

static GLint vp[4];
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { REAL(void, glViewport, GLint, GLint, GLsizei, GLsizei); vp[0] = x; vp[1] = y; vp[2] = w; vp[3] = h; r_glViewport(x, y, w, h); }
static void drawlog(const char *fn, GLenum mode, GLsizei cnt) {
    static int dl = -1; if (dl < 0) dl = envi("HM1GL_DRAWLOG", 0);
    if (!dl) return;
    // only log from HM1GL_DRAWLOG seconds after the first draw (to skip the menus), at most 6000 lines
    static long t0; struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); if (!t0) t0 = ts.tv_sec;
    if (ts.tv_sec - t0 < dl || !cur_fbo || cur_att == 1) return;
    { const char *sk = getenv("HM1GL_DRAWSKIP"); char tag[16]; snprintf(tag, sizeof tag, ",%u,", cur_att);
      if (sk) { char buf[128]; snprintf(buf, sizeof buf, ",%s,", sk); if (strstr(buf, tag)) return; } }
    static int n; if (n++ > 6000) return;
    REAL(void, glGetIntegerv, GLenum, GLint *);
    GLint tex = 0, unit = 0, vpp = 0, fpp = 0; r_glGetIntegerv(0x8069, &tex); r_glGetIntegerv(0x84E0, &unit);
    REAL(void, glGetProgramivARB, GLenum, GLenum, GLint *);
    r_glGetProgramivARB(0x8620, 0x8677, &vpp); r_glGetProgramivARB(0x8804, 0x8677, &fpp);
    char lb[96]; extern const char *lastbind(char *, size_t);
    fprintf(stderr, "[draw] surf %u %s mode %u cnt %d tex %d unit 0x%x vp %d,%d %dx%d vprog %d fprog %d last %s\n", cur_att, fn, mode, cnt, tex, unit, vp[0], vp[1], vp[2], vp[3], vpp, fpp, lastbind(lb, sizeof lb));
}

// HM1GL_BAKEFIX=1: Phyre's mesh renderer (used only for the tile-floor bake in TileSet::RefreshTiles) binds
// FBO 0 right before drawing, so the floor tiles land in the window instead of the floor surface still
// attached to FBO 1. Detect that (FBO 0 bound, viewport = an offscreen surface size, not the window) and
// draw into FBO 1 instead.
static int bake_begin(void) {
    static int on_ = -1; if (on_ < 0) on_ = envi("HM1GL_BAKEFIX", 1);
    if (!on_ || cur_fbo != 0 || cur_att == 0 || cur_att == 1) return 0;
    if (vp[2] == 720 || vp[2] <= 0) return 0;          // window-sized draws are real window draws
    REAL(void, glBindFramebufferEXT, GLenum, GLuint); r_glBindFramebufferEXT(0x8D40, 1);
    static int n; if (n++ < 20) fprintf(stderr, "[glspy] BAKEFIX: redirected draw (vp %dx%d) into surface tex %u\n", vp[2], vp[3], cur_att);
    return 1;
}
static void bake_end(int b) { if (b) { REAL(void, glBindFramebufferEXT, GLenum, GLuint); r_glBindFramebufferEXT(0x8D40, 0); } }
void glDrawArrays(GLenum m, GLint f, GLsizei c) { REAL(void, glDrawArrays, GLenum, GLint, GLsizei); { extern void trx(const char*,unsigned,unsigned,unsigned,unsigned); trx("DrawArrays", m, f, c, 0); } drawlog("DrawArrays", m, c); { int bf = bake_begin(); r_glDrawArrays(m, f, c); bake_end(bf); } }
void glDrawElements(GLenum m, GLsizei c, GLenum t, const void *i) { REAL(void, glDrawElements, GLenum, GLsizei, GLenum, const void *); { extern void trx(const char*,unsigned,unsigned,unsigned,unsigned); trx("DrawElements", m, c, t, (unsigned)(uintptr_t)i); } drawlog("DrawElements", m, c); { int bf = bake_begin(); r_glDrawElements(m, c, t, i); bake_end(bf); } }
void glDrawRangeElements(GLenum m, GLuint s, GLuint e, GLsizei c, GLenum t, const void *i) { REAL(void, glDrawRangeElements, GLenum, GLuint, GLuint, GLsizei, GLenum, const void *); { extern void trx(const char*,unsigned,unsigned,unsigned,unsigned); trx("DrawRangeElements", s, e, c, (unsigned)(uintptr_t)i); } drawlog("DrawRangeElements", m, c); { int bf = bake_begin(); r_glDrawRangeElements(m, s, e, c, t, i); bake_end(bf); } }
// other ways of putting pixels into the current offscreen surface
static void offlog(const char *fn, int a, int b, int c, int d) {
    static int dl = -1; if (dl < 0) dl = envi("HM1GL_DRAWLOG", 0);
    if (!dl || !cur_fbo || cur_att == 1 || cur_att == 71 || cur_att == 79) return;
    static int n; if (n++ > 3000) return;
    GLint tex = 0; REAL(void, glGetIntegerv, GLenum, GLint *); r_glGetIntegerv(0x8069, &tex);
    fprintf(stderr, "[off] surf %u %s %d %d %d %d tex %d\n", cur_att, fn, a, b, c, d, tex);
}
void glBegin(GLenum m) { REAL(void, glBegin, GLenum); offlog("glBegin", m, 0, 0, 0); r_glBegin(m); }
void glBlitFramebufferEXT(GLint a, GLint b, GLint c, GLint d, GLint e, GLint f, GLint g, GLint h, GLbitfield m, GLenum fl) { REAL(void, glBlitFramebufferEXT, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum); offlog("glBlitFramebufferEXT", e, f, g, h); r_glBlitFramebufferEXT(a, b, c, d, e, f, g, h, m, fl); }
void glBlitFramebuffer(GLint a, GLint b, GLint c, GLint d, GLint e, GLint f, GLint g, GLint h, GLbitfield m, GLenum fl) { REAL(void, glBlitFramebuffer, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum); offlog("glBlitFramebuffer", e, f, g, h); r_glBlitFramebuffer(a, b, c, d, e, f, g, h, m, fl); }
void glDrawArraysEXT(GLenum m, GLint f, GLsizei c) { REAL(void, glDrawArraysEXT, GLenum, GLint, GLsizei); offlog("glDrawArraysEXT", m, c, 0, 0); r_glDrawArraysEXT(m, f, c); }
void glMultiDrawArrays(GLenum m, const GLint *f, const GLsizei *c, GLsizei n) { REAL(void, glMultiDrawArrays, GLenum, const GLint *, const GLsizei *, GLsizei); offlog("glMultiDrawArrays", m, n, 0, 0); r_glMultiDrawArrays(m, f, c, n); }
void glMultiDrawElements(GLenum m, const GLsizei *c, GLenum t, const void *const *i, GLsizei n) { REAL(void, glMultiDrawElements, GLenum, const GLsizei *, GLenum, const void *const *, GLsizei); offlog("glMultiDrawElements", m, n, 0, 0); r_glMultiDrawElements(m, c, t, i, n); }
void glCallList(GLuint l) { REAL(void, glCallList, GLuint); offlog("glCallList", l, 0, 0, 0); r_glCallList(l); }

// who bound the texture a draw ends up using
static GLuint lb_tex; static const char *lb_src = "-"; static GLenum lb_unit;
void glBindTexture(GLenum t, GLuint tex) { REAL(void, glBindTexture, GLenum, GLuint); lb_tex = tex; lb_src = "BindTexture"; r_glBindTexture(t, tex); }
void glActiveTexture(GLenum u) { REAL(void, glActiveTexture, GLenum); lb_unit = u; r_glActiveTexture(u); }
void glActiveTextureARB(GLenum u) { REAL(void, glActiveTextureARB, GLenum); lb_unit = u; r_glActiveTextureARB(u); }
const char *lastbind(char *b, size_t n) { snprintf(b, n, "%s %u (active 0x%x)", lb_src, lb_tex, lb_unit); return b; }
void set_lb(GLuint t, const char *src) { lb_tex = t; lb_src = src; }
int trace_left;
static void tr(const char *what, unsigned a, unsigned b, unsigned c, unsigned d) {
    if (trace_left <= 0) return; trace_left--;
    GLint tex = 0; REAL(void, glGetIntegerv, GLenum, GLint *); r_glGetIntegerv(0x8069, &tex);
    static void *(*egc)(void); if (!egc) egc = dlsym(RTLD_DEFAULT, "eglGetCurrentContext");
    fprintf(stderr, "[trace] t%ld%s %s %u %u %u %u | fbo %u att %u tex %d vp %dx%d\n", (long)syscall(224), egc && !egc() ? " NOCTX" : "", what, a, b, c, d, cur_fbo, cur_att, tex, vp[2], vp[3]);
}
void trx(const char *w, unsigned a, unsigned b, unsigned c, unsigned d) { tr(w, a, b, c, d); }
