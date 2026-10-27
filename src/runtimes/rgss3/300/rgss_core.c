/*
 * rgss_core.c — RGSS3 runtime skeleton (M3 stage)
 *
 * This file implements the 27 exported RGSS300.dll entry points with the
 * exact semantics recovered in M2 (see docs/rgss3/RGSS3_M2_Game.exe接口文档.md).
 *
 * M3 scope: prove that the embedded Ruby 1.9.2p0 interpreter links and that
 * the eval/data-bridge layer behaves correctly. Object layer (Bitmap/Sprite/
 * Viewport/... = CRx* classes), graphics backend (NxDrawLocal) and audio
 * backend (CSx*) land in M4/M5.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <ruby.h>

#include "RGSS300.h"

/* ------------------------------------------------------------------ */
/* interpreter lifecycle                                               */
/* ------------------------------------------------------------------ */

HINSTANCE g_hInstance = NULL;

/*
 * Rebuilt from sub_1000CCA0: sysinit -> ruby_init -> encoding default ->
 * load_data/save_data global functions -> RGSS_VERSION const -> module inits
 * -> UI-language dependent string-table base (0 = eng/chs, 100 = jpn).
 */
int g_stringTableBase = 0;          /* original: word_10263020 */

static int  rgss_ui_language(void);

static VALUE rgss_load_data(VALUE path)
{
    /* M4: Marshal.load from the game's Data/ directory */
    (void)path;
    return Qnil;
}

static VALUE rgss_save_data(VALUE path, VALUE obj)
{
    /* M4: Marshal.dump into the game's Data/ directory */
    (void)path; (void)obj;
    return Qnil;
}

HINSTANCE RGSSInitialize3(HINSTANCE hInst)
{
    static int argc = 1;
    static char arg0[] = "RGSS300";
    static char *argv[] = { arg0, NULL };

    g_hInstance = hInst;

    ruby_sysinit((int *)&argc, (char ***)&argv);
    RUBY_INIT_STACK;
    ruby_init();
    ruby_init_loadpath();

    /* default internal encoding (original: sub_1000C580 with "Encoding.default") */
    rb_eval_string("Encoding.default_internal = nil rescue nil");

    rb_define_global_function("load_data", rgss_load_data, 1);
    rb_define_global_function("save_data", rgss_save_data, 2);

    rb_eval_string("RGSS_VERSION = '3.00' unless defined?(RGSS_VERSION)");

    g_stringTableBase = (rgss_ui_language() == 17) ? 100 : 0;

    return hInst;
}

static int rgss_ui_language(void)
{
    /* original: GetUserDefaultUILanguage() & 0x3FF */
    return (int)(GetUserDefaultUILanguage() & 0x3FF);
}

void RGSSFinalize(void)
{
    /* original: clears the run flag only; interpreter shutdown happens later */
}

void RGSSGC(void)
{
    rb_gc_start();
}

/* ------------------------------------------------------------------ */
/* eval bridge                                                         */
/* ------------------------------------------------------------------ */

/* original: sub_10030220 — rb_eval_string_protect wrapper.
 * Returns VALUE and writes the protect state to *err. */
static VALUE eval_wrapper(const char *expr, int *err)
{
    if (!expr) { if (err) *err = 1; return Qnil; }
    return rb_eval_string_protect(expr, err);
}

int RGSSEval(const char *expr)
{
    int err = 0;
    eval_wrapper(expr, &err);
    return err;
}

/* original: strictly == Qtrue (1.9 constants: false=0, nil=4, true=2) */
BOOL RGSSGetBool(const char *expr)
{
    int err = 0;
    VALUE v = eval_wrapper(expr, &err);
    if (err) return FALSE;
    return (v == Qtrue) ? TRUE : FALSE;
}

int RGSSGetInt(const char *expr)
{
    int err = 0;
    VALUE v = eval_wrapper(expr, &err);
    if (err) return 0;
    switch (rb_type(v)) {
      case T_FIXNUM:
      case T_BIGNUM:
        return NUM2INT(v);
      case T_FLOAT:
        return (int)NUM2DBL(v);
      default:
        return 0;
    }
}

double RGSSGetDouble(const char *expr)
{
    int err = 0;
    VALUE v = eval_wrapper(expr, &err);
    if (err) return 0.0;
    switch (rb_type(v)) {
      case T_FIXNUM:
      case T_BIGNUM:
      case T_FLOAT:
        return NUM2DBL(v);
      default:
        return 0.0;
    }
}

/* original: Check_Type(v, T_DATA); out = data[3],[4],[5],[7] */
void RGSSGetTable(unsigned int out[4], const char *expr)
{
    int err = 0;
    VALUE v;
    out[0] = out[1] = out[2] = out[3] = 0;
    v = eval_wrapper(expr, &err);
    if (err) return;
    if (rb_type(v) != T_DATA) return;
    {
        const unsigned int *d = (const unsigned int *)DATA_PTR(v);
        if (!d) return;
        out[0] = d[3]; out[1] = d[4]; out[2] = d[5]; out[3] = d[7];
    }
}

/* original: T_SYMBOL -> rb_id2str(v>>8) */
const char *RGSSGetSymbol(const char *expr)
{
    static char empty[1] = { 0 };
    int err = 0;
    VALUE v = eval_wrapper(expr, &err);
    if (err) return empty;
    if (rb_type(v) != T_SYMBOL) return empty;
    return rb_id2name(SYM2ID(v));
}

const char *RGSSGetStringUTF8(const char *expr)
{
    static char empty[1] = { 0 };
    int err = 0;
    VALUE v = eval_wrapper(expr, &err);
    if (err) return empty;
    if (rb_type(v) != T_STRING) return empty;
    return RSTRING_PTR(v);
}

/* original: UTF8 -> UTF-16 -> CP_ACP; buffers are 4*len (原版同样式).
 * Result lives in a rotating static buffer because the C ABI returns char*. */
#define RGSS_STRING_RING 8
#define RGSS_STRING_MAX  4096
static char g_acp_ring[4][RGSS_STRING_MAX];
static int  g_acp_slot = 0;
static WCHAR g_utf16_ring[4][RGSS_STRING_MAX / 2];
static int  g_utf16_slot = 0;

const char *RGSSGetStringACP(const char *expr)
{
    static char empty[1] = { 0 };
    int err = 0;
    VALUE v = eval_wrapper(expr, &err);
    const char *utf8;
    int len;
    char *out;

    if (err) return empty;
    if (rb_type(v) != T_STRING) return empty;
    utf8 = RSTRING_PTR(v);
    len = (int)strlen(utf8);
    if (len <= 0) return empty;

    out = g_acp_ring[g_acp_slot++ & 3];
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, g_utf16_ring[g_utf16_slot & 3],
                        RGSS_STRING_MAX / 2);
    WideCharToMultiByte(CP_ACP, 0, g_utf16_ring[g_utf16_slot & 3], -1,
                        out, RGSS_STRING_MAX, NULL, NULL);
    g_utf16_slot++;
    out[RGSS_STRING_MAX - 1] = 0;
    return out;
}

const char *RGSSGetStringUTF16(const char *expr)
{
    static char empty[1] = { 0 };
    int err = 0;
    VALUE v = eval_wrapper(expr, &err);
    const char *utf8;
    WCHAR *out;

    if (err) return empty;
    if (rb_type(v) != T_STRING) return empty;
    utf8 = RSTRING_PTR(v);
    if (!*utf8) return empty;

    out = g_utf16_ring[(g_utf16_slot++) & 3];
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, RGSS_STRING_MAX / 2);
    return (const char *)out;
}

/* SetString family: evaluate the destination expression, expect T_STRING,
 * then replace its contents (original: rb_str replace helpers). */
static int set_string_impl(const char *expr, const void *data, long len,
                           const WCHAR *wide, int is_wide)
{
    int err = 0;
    VALUE v;

    if (!expr || !data) return 0;
    v = eval_wrapper(expr, &err);
    if (err) return 0;
    if (rb_type(v) != T_STRING) return 0;

    if (!is_wide) {
        rb_str_resize(v, 0);
        rb_str_cat(v, (const char *)data, len);
    } else {
        /* wide -> UTF-8 */
        int need = WideCharToMultiByte(CP_UTF8, 0, wide, (int)len, NULL, 0, NULL, NULL);
        char *tmp = (char *)malloc(need + 2);
        if (!tmp) return 0;
        WideCharToMultiByte(CP_UTF8, 0, wide, (int)len, tmp, need, NULL, NULL);
        rb_str_resize(v, 0);
        rb_str_cat(v, tmp, need);
        free(tmp);
    }
    return 1;
}

int RGSSSetString(const char *expr, const void *data, size_t len)
{
    return set_string_impl(expr, data, (long)len, NULL, 0);
}

int RGSSSetStringUTF8(const char *expr, const char *str)
{
    if (!str) return 0;
    return set_string_impl(expr, str, (long)strlen(str), NULL, 0);
}

int RGSSSetStringACP(const char *expr, const char *str)
{
    WCHAR wide[RGSS_STRING_MAX / 2];
    if (!str) return 0;
    if (!MultiByteToWideChar(CP_ACP, 0, str, -1, wide, RGSS_STRING_MAX / 2))
        return 0;
    return set_string_impl(expr, str, (long)wcslen(wide), wide, 1);
}

int RGSSSetStringUTF16(const char *expr, const wchar_t *str)
{
    if (!str) return 0;
    return set_string_impl(expr, str, (long)wcslen(str), str, 1);
}

/* ------------------------------------------------------------------ */
/* error reporting (wide buffers, 1024)                                */
/* ------------------------------------------------------------------ */

static WCHAR g_errorType[1024];
static WCHAR g_errorMessage[1024];

wchar_t *RGSSErrorType(void)
{
    return g_errorType;
}

wchar_t *RGSSErrorMessage(void)
{
    return g_errorMessage;
}

/* ------------------------------------------------------------------ */
/* main loop                                                           */
/* ------------------------------------------------------------------ */

/*
 * original sub_10001620: construct CGameFrame, subclass WndProc, init screen
 * (544x416 internal), verify OS/DirectX/CPU/bpp/audio, then enter the real
 * loop sub_1000E9E0(scriptsPath, flags). M4/M5 fill the frame and loop.
 */
void RGSSGameMain(HWND hwnd, const wchar_t *scriptsPath, int flags)
{
    (void)hwnd; (void)scriptsPath; (void)flags;
    /* M4: real loop; for now run the game's script if provided via RGSSEval. */
}

/* ------------------------------------------------------------------ */
/* RTP / fonts                                                         */
/* ------------------------------------------------------------------ */

static WCHAR g_rtpPath[260];
static WCHAR g_pathWithRTP[520];

int RGSSClearRTPPath(void) { g_rtpPath[0] = 0; return 0; }

int RGSSAddRTPPath(const wchar_t *path)
{
    if (!path || !*path) return 0;
    if (wcslen(path) >= 260) return 0;
    wcscpy(g_rtpPath, path);
    {
        size_t n = wcslen(g_rtpPath);
        if (n && g_rtpPath[n - 1] == L'\\') g_rtpPath[n - 1] = 0;
    }
    return 0;
}

int RGSSGetRTPPath(void *out)
{
    if (out) wcscpy((WCHAR *)out, g_rtpPath);
    return g_rtpPath[0] != 0;
}

wchar_t *RGSSGetPathWithRTP(const wchar_t *name)
{
    size_t n;
    if (!name || !(n = wcslen(name)) || n >= 260) {
        g_pathWithRTP[0] = 0;
        return g_pathWithRTP;
    }
    if (g_rtpPath[0]) {
        _snwprintf(g_pathWithRTP, 519, L"%s\\%s", g_rtpPath, name);
        g_pathWithRTP[519] = 0;
    } else {
        wcscpy(g_pathWithRTP, name);
    }
    return g_pathWithRTP;
}

/*
 * original: for i=1..9 read Game.ini [Game] RTP / RTP%d, resolve each name
 * through HKLM Software\Enterbrain\RGSS3\RTP, strip trailing backslash, then
 * add the path and register its Fonts directory.
 */
int RGSSSetupRTP(const wchar_t *iniPath, wchar_t *outName, size_t size)
{
    HKEY key = NULL;
    int i;

    RGSSClearRTPPath();
    if (outName && size) outName[0] = 0;

    RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Enterbrain\\RGSS3\\RTP", 0,
                  KEY_READ, &key);
    for (i = 1; i <= 9; i++) {
        WCHAR keyName[32];
        WCHAR iniName[1024];
        WCHAR regPath[260];
        DWORD cbData = sizeof(regPath);
        DWORD type = 0;

        if (i == 1) wcscpy(keyName, L"RTP");
        else        _snwprintf(keyName, 31, L"RTP%d", i), keyName[31] = 0;

        iniName[0] = 0;
        GetPrivateProfileStringW(L"Game", keyName, L"", iniName, 1024, iniPath);
        if (!iniName[0]) continue;

        if (key) {
            if (RegQueryValueExW(key, iniName, NULL, &type, (LPBYTE)regPath,
                                 &cbData) == ERROR_SUCCESS && type != REG_SZ) {
                regPath[0] = 0;
            }
        } else {
            regPath[0] = 0;
        }

        if (!regPath[0]) {
            if (outName && size) {
                wcsncpy(outName, iniName, size - 1);
                outName[size - 1] = 0;
            }
            continue;
        }
        {
            size_t n = wcslen(regPath);
            if (n && regPath[n - 1] == L'\\') regPath[n - 1] = 0;
        }
        RGSSAddRTPPath(regPath);
        RGSSSetupFonts();
    }
    if (key) RegCloseKey(key);
    return g_rtpPath[0] ? 0 : 1;
}

int RGSSSetupFonts(void)
{
    /* original: register the RTP's "Fonts" directory (AddFontResourceExW) */
    if (g_rtpPath[0]) {
        WCHAR dir[520];
        _snwprintf(dir, 519, L"%s\\Fonts", g_rtpPath);
        dir[519] = 0;
        AddFontResourceExW(dir, FR_PRIVATE, 0);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* audio (M5 replaces this skeleton with CSx* mirror)                  */
/* ------------------------------------------------------------------ */

static int g_audioInited = 0;

int RGSSAudioInitialize(HWND hwnd)
{
    (void)hwnd;
    g_audioInited = 1;
    return 1;
}

void RGSSAudioFinalize(void)
{
    g_audioInited = 0;
}

/* ------------------------------------------------------------------ */
/* DLL entry                                                           */
/* ------------------------------------------------------------------ */

/*
 * NOTE: libruby192-static.a already provides DllMain (ruby.c:318) which sets
 * up the interpreter's TLS. Defining our own here would collide, so the
 * library's entry point is used as-is.
 */
