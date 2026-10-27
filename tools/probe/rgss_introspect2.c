/* rgss_introspect2.c — 触发 RGSS 图形子系统后再反射 */
#include <windows.h>
#include <stdio.h>

typedef int (*FnInit)(int);
typedef int (*FnEval)(const char *);
typedef const char *(*FnGetStr)(const char *);

static FnEval g_eval;
static FnGetStr g_getstr;

static void show(const char *label, const char *code)
{
    g_eval(code);
    printf("%-30s => %s\n", label, g_getstr("($!.nil? ? $__r : ($__r=$!.message))"));
}

static void showv(const char *label, const char *expr)
{
    g_eval("$__r = begin; (" );
    char buf[2048];
    sprintf(buf, "$__r = begin; %s; rescue => e; \"ERR: #{e.class}: #{e.message}\"; end", expr);
    g_eval(buf);
    printf("%-30s => %s\n", label, g_getstr("$__r.inspect"));
}

int main(int argc, char **argv)
{
    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) return 1;
    FnInit init = (FnInit)GetProcAddress(h, "RGSSInitialize2");
    g_eval = (FnEval)GetProcAddress(h, "RGSSEval");
    g_getstr = (FnGetStr)GetProcAddress(h, "RGSSGetStringUTF8");
    if (!init) return 1;
    init(0);

    puts("===== 未初始化图形时的类状态 =====");
    showv("Bitmap.class", "Bitmap.class.to_s");
    showv("Bitmap.superclass", "Bitmap.superclass.to_s");
    showv("Bitmap.instance_methods.size", "Bitmap.instance_methods.size");
    showv("Bitmap.new(1,1)", "Bitmap.new(1,1).class.to_s");
    showv("Graphics.class", "Graphics.class.to_s");
    showv("Graphics.respond_to?(:update)", "Graphics.respond_to?(:update).to_s");
    showv("Graphics.methods", "Graphics.methods(false).sort.join(' ')");

    puts("\n===== 尝试初始化图形 (Graphics.update) =====");
    showv("Graphics.update", "Graphics.update; 'ok'");
    showv("Graphics.frame_count", "Graphics.frame_count.to_s");
    showv("Graphics.frame_rate", "Graphics.frame_rate.to_s");

    puts("\n===== 再次检查 =====");
    showv("Bitmap.instance_methods", "Bitmap.instance_methods(false).sort.join(' ')");
    showv("Graphics.methods", "Graphics.methods(false).sort.join(' ')");
    showv("Input.methods", "Input.methods(false).sort.join(' ')");

    puts("\n===== 直接看类对象的内部 =====");
    showv("Bitmap inspect", "Bitmap.inspect");
    showv("Sprite inspect", "Sprite.inspect");
    showv("Graphics inspect", "Graphics.inspect");

    puts("\n===== 方法定义来源 =====");
    showv("Bitmap 是否 Data?", "Bitmap.superclass.to_s");
    showv("Font.new", "Font.new.inspect rescue 'fail'");
    showv("Color.new", "Color.new(1,2,3).inspect");
    showv("Rect.new", "Rect.new(1,2,3,4).inspect");
    return 0;
}
