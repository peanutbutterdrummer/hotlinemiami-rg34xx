// cgspy.so (i386, loaded by box86 through BOX86_LD_PRELOAD): logs the Cg runtime calls Hotline Miami
// makes (profiles, program creation, compiler listing, errors) and can override profile choice:
// CGSPY_VP / CGSPY_FP = profile number to return from cgGLGetLatestProfile (e.g. arbvp1 6150, arbfp1 7000,
// glslv 7007, glslf 7008).
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

typedef void *CGcontext, *CGprogram, *CGeffect;
typedef int CGprofile, CGenum, CGerror, CGGLenum, CGbool;

static void *cg, *cggl;
static void *S(void **h, const char *lib, const char *n) {
    if (!*h) *h = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
    void *f = *h ? dlsym(*h, n) : NULL;
    if (!f) fprintf(stderr, "[cgspy] missing %s in %s\n", n, lib);
    return f;
}
#define CG(ret, n, ...) static ret (*r_##n)(__VA_ARGS__); if (!r_##n) r_##n = S(&cg, "libCg.so", #n)
#define CGGL(ret, n, ...) static ret (*r_##n)(__VA_ARGS__); if (!r_##n) r_##n = S(&cggl, "libCgGL.so", #n)

static const char *errstr(void) {
    CG(CGerror, cgGetError, void);
    CG(const char *, cgGetErrorString, CGerror);
    CGerror e = r_cgGetError();
    return e ? r_cgGetErrorString(e) : "ok";
}
static void listing(CGcontext c) {
    CG(const char *, cgGetLastListing, CGcontext);
    const char *l = c ? r_cgGetLastListing(c) : NULL;
    if (l && *l) fprintf(stderr, "[cgspy] listing:\n%s\n", l);
}
static const char *pname(CGprofile p) {
    CG(const char *, cgGetProfileString, CGprofile);
    const char *s = r_cgGetProfileString ? r_cgGetProfileString(p) : NULL;
    return s ? s : "?";
}

CGprofile cgGLGetLatestProfile(CGGLenum cls) {
    CGGL(CGprofile, cgGLGetLatestProfile, CGGLenum);
    CGprofile p = r_cgGLGetLatestProfile(cls);
    const char *o = getenv(cls == 8 ? "CGSPY_VP" : "CGSPY_FP");   // CG_GL_VERTEX = 8, CG_GL_FRAGMENT = 9
    CGprofile q = o ? atoi(o) : p;
    fprintf(stderr, "[cgspy] cgGLGetLatestProfile(%d) = %d %s%s\n", cls, p, pname(p), q != p ? " (overridden)" : "");
    if (q != p) fprintf(stderr, "[cgspy]   -> %d %s\n", q, pname(q));
    return q;
}
CGbool cgGLIsProfileSupported(CGprofile p) {
    CGGL(CGbool, cgGLIsProfileSupported, CGprofile);
    CGbool r = r_cgGLIsProfileSupported(p);
    fprintf(stderr, "[cgspy] cgGLIsProfileSupported(%d %s) = %d\n", p, pname(p), r);
    return r;
}
CGprogram cgCreateProgram(CGcontext c, CGenum type, const char *src, CGprofile p, const char *entry, const char **args) {
    CG(CGprogram, cgCreateProgram, CGcontext, CGenum, const char *, CGprofile, const char *, const char **);
    CGprogram r = r_cgCreateProgram(c, type, src, p, entry, args);
    fprintf(stderr, "[cgspy] cgCreateProgram(type %d, profile %d %s, entry %s) = %p [%s]\n", type, p, pname(p),
            entry ? entry : "(null)", r, errstr());
    for (int i = 0; args && args[i] && i < 16; i++) fprintf(stderr, "[cgspy]   arg %s\n", args[i]);
    if (!r || getenv("CGSPY_SRC")) { listing(c); if (src && getenv("CGSPY_SRC")) fprintf(stderr, "%.600s\n", src); }
    return r;
}
CGprogram cgCreateProgramFromEffect(CGeffect e, CGprofile p, const char *entry, const char **args) {
    CG(CGprogram, cgCreateProgramFromEffect, CGeffect, CGprofile, const char *, const char **);
    CGprogram r = r_cgCreateProgramFromEffect(e, p, entry, args);
    fprintf(stderr, "[cgspy] cgCreateProgramFromEffect(profile %d %s, entry %s) = %p [%s]\n", p, pname(p),
            entry ? entry : "(null)", r, errstr());
    return r;
}
void cgGLLoadProgram(CGprogram prog) {
    CGGL(void, cgGLLoadProgram, CGprogram);
    r_cgGLLoadProgram(prog);
    fprintf(stderr, "[cgspy] cgGLLoadProgram(%p) [%s]\n", prog, errstr());
}
