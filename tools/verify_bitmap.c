/* verify_bitmap.c — 用原版 DLL 运行时验证 Bitmap 布局推断 */
#include <windows.h>
#include <stdio.h>

typedef int (*FnInit)(int);
typedef int (*FnEval)(const char *);
typedef const char *(*FnGetStr)(const char *);

static FnEval g_eval;
static FnGetStr g_getstr;

static void t(const char *label, const char *code)
{
    char buf[2048];
    sprintf(buf, "$__r = begin; %s; rescue => e; \"ERR: #{e.class}: #{e.message}\"; end", code);
    g_eval(buf);
    printf("%-40s => %s\n", label, g_getstr("$__r.inspect"));
}

int main(int argc, char **argv)
{
    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) return 1;
    FnInit init = (FnInit)GetProcAddress(h, "RGSSInitialize2");
    g_eval = (FnEval)GetProcAddress(h, "RGSSEval");
    g_getstr = (FnGetStr)GetProcAddress(h, "RGSSGetStringUTF8");
    init(0);

    puts("===== 前置: 图形类注册(需触发阶段B) =====");
    t("Graphics.update", "Graphics.update; 'ok'");

    puts("\n===== Bitmap 基本 =====");
    t("Bitmap.new(8,4)", "$b = Bitmap.new(8,4); [$b.width, $b.height].inspect");
    t("rect", "$b.rect.inspect");
    t("disposed?", "$b.disposed?");

    puts("\n===== 像素格式验证 (ARGB8888) =====");
    t("set red 255,0,0,255", "$b.set_pixel(0,0,Color.new(255,0,0,255)); $b.get_pixel(0,0).inspect");
    t("set green 0,255,0,128", "$b.set_pixel(1,0,Color.new(0,255,0,128)); $b.get_pixel(1,0).inspect");
    t("set blue 0,0,255,0", "$b.set_pixel(2,0,Color.new(0,0,255,0)); $b.get_pixel(2,0).inspect");

    puts("\n===== 越界行为 (推断: 静默返回 0,0,0,0) =====");
    t("get_pixel(-1,0)", "$b.get_pixel(-1,0).inspect");
    t("get_pixel(99,0)", "$b.get_pixel(99,0).inspect");
    t("get_pixel(0,-1)", "$b.get_pixel(0,-1).inspect");
    t("get_pixel(0,99)", "$b.get_pixel(0,99).inspect");
    t("set_pixel 越界", "$b.set_pixel(-5,-5,Color.new(1,2,3)); 'no raise'");

    puts("\n===== clear / fill_rect =====");
    t("clear", "$b.clear; $b.get_pixel(0,0).inspect");
    t("fill_rect", "$b.fill_rect(0,0,8,4,Color.new(10,20,30,40)); $b.get_pixel(7,3).inspect");

    puts("\n===== dispose 后行为 =====");
    t("dispose", "$b.dispose; $b.disposed?");
    t("dispose后 width", "$b.width");
    t("dispose后 get_pixel", "$b.get_pixel(0,0)");

    puts("\n===== text_size / draw_text =====");
    t("Font default", "Font.default_name.inspect + ' ' + Font.default_size.to_s");
    t("Bitmap#text_size", "$c = Bitmap.new(100,50); $c.text_size('Hello').inspect");
    t("draw_text 不崩", "$c.draw_text(0,0,100,20,'Hello'); 'ok'");
    t("font=", "$f=Font.new; $f.size=20; $c.font=$f; $c.text_size('Hello').inspect");

    puts("\n===== blt =====");
    t("blt", "$d=Bitmap.new(4,4); $d.blt(0,0,$b,Rect.new(0,0,4,4)); 'ok'");
    t("stretch_blt", "$d.stretch_blt(Rect.new(0,0,4,4),$b,Rect.new(0,0,4,4)); 'ok'");
    t("hue_change", "$d.hue_change(30); 'ok'");
    t("blur", "$d.blur; 'ok'");
    t("radial_blur", "$d.radial_blur(30,10); 'ok'");
    return 0;
}
