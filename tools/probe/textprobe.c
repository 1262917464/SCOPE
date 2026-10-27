/* textprobe.c — 在纯色底上画字并导出原始 RGBA, 对比原版/重建版 draw_text
 * 用法: textprobe.exe <dll路径> <输出.bin>
 *
 * 原版 DLL 的图形类注册在 GameMain 内, 而 GameMain 用假窗口会崩
 * (phaseb.c 实测), 故直接按 M5-b 总表地址调用各 Init 函数:
 *   Color 0x100068C0  Tone 0x100101A0  Rect 0x1000C910  Table 0x1000EFA0
 *   Font 0x100075D0   Bitmap 0x10006060
 * 重建版走 Graphics.update 的两阶段注册。 */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (*FnInit)(int);
typedef int (*FnEval)(const char *);
typedef int (*FnGetStr)(const char *);
typedef void (*FnVoid)(void);

static const char *SCRIPT =
    "$r = begin;"
    "bg = Color.new(40,80,160,255);"
    "b = Bitmap.new(220,80);"
    "b.fill_rect(0,0,220,80,bg);"
    "b.font.size = 22;"
    "b.font.name = [\"\xE9\x9C\x9E\xE9\xB9\x9C\xE6\x96\x87\xE6\xA5\xB7\xE7\xAD\x89\xE5\xAE\xBD TC Bold\","
    "\"\xE9\x9C\x9E\xE9\xB9\x9C\xE6\x96\x87\xE6\xA5\xB7 \xE5\xB1\x8F\xE5\xB9\x95\xE9\x98\x85\xE8\xAF\xBB\xE7\x89\x88 R\","
    "\"LXGW WenKai Mono TC\",\"DFKai-SB\","
    "\"\xE6\xA8\x99\xE6\xA5\xB7\xE9\xAB\x94\",\"PMingLiU\","
    "\"\xE6\x96\xB0\xE7\xB4\xB0\xE6\x98\x8E\xE9\xAB\x94\",\"Verdana\",\"Arial Unicode MS\"];"
    "b.font.color = Color.new(255,255,255,255);"
    "b.font.shadow = true;"
    "b.draw_text(4,6,210,30,\"\xE7\x89\xB9\xE6\xAE\x8A\xE6\x8A\x80\xE8\x83\xBD\",0);"
    "b.font.shadow = false;"
    "b.draw_text(4,40,210,30,\"\xE7\x8B\x80\xE6\x85\x8B\xE8\xA3\x9D\xE5\x82\x99\",0);"
    "s = [220,80].pack('l*');"
    "80.times{|y| 220.times{|x| c=b.get_pixel(x,y);"
    "s << c.red.to_i.chr << c.green.to_i.chr << c.blue.to_i.chr << c.alpha.to_i.chr}};"
    "File.open(\"__TEXTOUT__\",\"wb\"){|f| f.write(s)};"
    "'ok';"
    "rescue Exception => e;"
    "'EXC:'+e.class.to_s+':'+e.message;"
    "end;";

static void call_inits(HMODULE h, uintptr_t pref_base)
{
    static const struct { uintptr_t va; const char *name; } inits[] = {
        { 0x10006950, "Color"  },   /* Init_Color 本体 (0x68C0 是 _dump) */
        { 0x10010210, "Tone"   },
        { 0x1000C910, "Rect"   },
        { 0x100075D0, "Font"   },
        { 0x10006060, "Bitmap" },
        { 0, NULL }
    };
    uintptr_t actual = (uintptr_t)h;
    for (int i = 0; inits[i].name; i++) {
        FnVoid f = (FnVoid)(actual + inits[i].va - pref_base);
        printf("[init] %s @%p\n", inits[i].name, (void *)f);
        f();
    }
}

int main(int argc, char **argv)
{
    HMODULE h;
    FnInit init;
    FnEval ev;
    FnGetStr gs;
    static char cmd[16384];
    const char *out;
    int n;
    const char *p;

    if (argc < 3) { printf("usage: textprobe <dll> <out.bin>\n"); return 2; }
    setvbuf(stdout, NULL, _IONBF, 0);
    out = argv[2];
    h = LoadLibraryA(argv[1]);
    if (!h) { printf("LoadLibrary fail %lu\n", GetLastError()); return 1; }
    init = (FnInit)GetProcAddress(h, "RGSSInitialize2");
    ev   = (FnEval)GetProcAddress(h, "RGSSEval");
    gs   = (FnGetStr)GetProcAddress(h, "RGSSGetStringUTF8");
    if (!init || !ev || !gs) { printf("missing exports\n"); return 1; }

    init(0);

    ev("$q=(defined?(Bitmap)||'no')");
    printf("[chk] Bitmap after init: %s\n", gs("$q"));
    if (strcmp(gs("$q"), "no") == 0) {
        printf("[init] calling Init fns directly\n");
        call_inits(h, 0x10000000);   /* 原版: 直接调 Init 本体 */
        ev("$q=(defined?(Bitmap)||'no')");
        printf("[chk] Bitmap after inits: %s\n", gs("$q"));
    }

    /* 逐步 eval, 精确定位崩溃点; 每步把标记写进 $m */
    static const char *steps[] = {
        "begin; $b=Bitmap.new(220,80); $m='bm'; rescue Exception=>$e; $m='EXC:'+$e.message; end",
        "begin; $b.fill_rect(0,0,220,80,Color.new(40,80,160,255)); $m='fill'; rescue Exception=>$e; $m='EXC:'+$e.message; end",
        "begin; $b.font.size=22; $m='size'; rescue Exception=>$e; $m='EXC:'+$e.message; end",
        "begin; $b.font.name=[\"\xE9\x9C\x9E\xE9\xB9\x9C\xE6\x96\x87\xE6\xA5\xB7 \xE5\xB1\x8F\xE5\xB9\x95\xE9\x98\x85\xE8\xAF\xBB\xE7\x89\x88 R\"]; $m='name'; rescue Exception=>$e; $m='EXC:'+$e.message; end",
        "begin; $b.font.color=Color.new(255,255,255,255); $m='color'; rescue Exception=>$e; $m='EXC:'+$e.message; end",
        "begin; $b.font.shadow=true; $m='shadow'; rescue Exception=>$e; $m='EXC:'+$e.message; end",
        "begin; $b.draw_text(4,6,210,30,\"\xE7\x89\xB9\xE6\xAE\x8A\xE6\x8A\x80\xE8\x83\xBD\",0); $m='draw1'; rescue Exception=>$e; $m='EXC:'+$e.message; end",
        "begin; $b.font.shadow=false; $b.draw_text(4,40,210,30,\"\xE7\x8B\x80\xE6\x85\x8B\xE8\xA3\x9D\xE5\x82\x99\",0); $m='draw2'; rescue Exception=>$e; $m='EXC:'+$e.message; end",
        "begin; $px=[220,80].pack('l*');"
        "80.times{|y| 220.times{|x| c=$b.get_pixel(x,y);"
        "$px << c.red.to_i.chr << c.green.to_i.chr << c.blue.to_i.chr << c.alpha.to_i.chr}}; $m='px';"
        "rescue Exception=>$e; $m='EXC:'+$e.message; end",
        NULL
    };
    for (int i = 0; steps[i]; i++) {
        ev(steps[i]);
        printf("[step %d] %s\n", i, gs("$m"));
    }
    {
        char io[512];
        sprintf(io, "begin; File.open(\"%s\",\"wb\"){|f| f.write($px)}; $m='saved'; rescue Exception=>$e; $m='EXC:'+$e.message; end", out);
        ev(io);
        printf("[save] %s\n", gs("$m"));
    }
    printf("done\n");
    fflush(stdout);
    ExitProcess(0);
}
