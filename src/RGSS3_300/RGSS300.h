/*
 * RGSS300.h — RGSS3 (VX Ace) runtime public surface, reconstructed.
 *
 * Source of truth: REA/IDA reverse engineering of RGSS300.dll (see
 *   docs/rgss3/RGSS3_M2_Game.exe接口文档.md).
 * All signatures below are the ones actually observed in the original
 * binary's calling convention (__cdecl for every export).
 */
#ifndef RGSS300_H
#define RGSS300_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- lifecycle / main loop ---- */
HINSTANCE RGSSInitialize3(HINSTANCE hInst);
void      RGSSFinalize(void);
void      RGSSGameMain(HWND hwnd, const wchar_t *scriptsPath, int flags);
void      RGSSGC(void);

/* ---- error reporting (wide-char buffers, 1024) ---- */
wchar_t  *RGSSErrorType(void);
wchar_t  *RGSSErrorMessage(void);

/* ---- eval + data bridge ---- */
int       RGSSEval(const char *expr);
BOOL      RGSSGetBool(const char *expr);
int       RGSSGetInt(const char *expr);
double    RGSSGetDouble(const char *expr);
void      RGSSGetTable(unsigned int out[4], const char *expr);
const char *RGSSGetSymbol(const char *expr);
const char *RGSSGetStringUTF8(const char *expr);
const char *RGSSGetStringACP(const char *expr);
const char *RGSSGetStringUTF16(const char *expr);
int       RGSSSetString(const char *expr, const void *data, size_t len);
int       RGSSSetStringUTF8(const char *expr, const char *str);
int       RGSSSetStringACP(const char *expr, const char *str);
int       RGSSSetStringUTF16(const char *expr, const wchar_t *str);

/* ---- RTP / fonts ---- */
int        RGSSSetupRTP(const wchar_t *iniPath, wchar_t *outName, size_t size);
int        RGSSAddRTPPath(const wchar_t *path);
int        RGSSClearRTPPath(void);
int        RGSSGetRTPPath(void *out);
wchar_t   *RGSSGetPathWithRTP(const wchar_t *name);
int        RGSSSetupFonts(void);

/* ---- audio ---- */
int       RGSSAudioInitialize(HWND hwnd);
void      RGSSAudioFinalize(void);

#ifdef __cplusplus
}
#endif
#endif /* RGSS300_H */
