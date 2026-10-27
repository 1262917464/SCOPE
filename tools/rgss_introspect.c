/* rgss_introspect.c — 用原版 RGSS DLL 的 RGSSEval 做运行时反射
 * 输出: 类层次、方法表、方法 arity、常量、Bitmap 对象内部布局
 */
#include <windows.h>
#include <stdio.h>

typedef int (*FnInit)(int);
typedef int (*FnEval)(const char *);
typedef const char *(*FnGetStr)(const char *);
typedef int (*FnGetInt)(const char *);

static FnEval g_eval;
static FnGetStr g_getstr;

static void run(const char *code)
{
    g_eval(code);
}

static void show(const char *label, const char *code)
{
    const char *r = g_getstr(code);
    printf("%-32s => %s\n", label, r ? r : "(null)");
}

int main(int argc, char **argv)
{
    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) { fprintf(stderr, "load fail %lu\n", GetLastError()); return 1; }
    FnInit init = (FnInit)GetProcAddress(h, "RGSSInitialize2");
    g_eval = (FnEval)GetProcAddress(h, "RGSSEval");
    g_getstr = (FnGetStr)GetProcAddress(h, "RGSSGetStringUTF8");
    FnGetInt getint = (FnGetInt)GetProcAddress(h, "RGSSGetInt");
    if (!init || !g_eval || !g_getstr) return 1;
    init(0);

    puts("===== 1. 类层次 =====");
    show("Object.constants", "Object.constants.sort.join(' ')");
    show("Bitmap.ancestors", "Bitmap.ancestors.join(' < ')");
    show("Sprite.ancestors", "Sprite.ancestors.join(' < ')");
    show("Graphics.ancestors", "Graphics.ancestors.join(' < ')");
    show("Audio.ancestors", "Audio.ancestors.join(' < ')");
    show("Input.ancestors", "Input.ancestors.join(' < ')");

    puts("\n===== 2. 各类方法表（含继承） =====");
    const char *klasses[] = { "Bitmap", "Sprite", "Plane", "Tilemap", "Window",
                              "Viewport", "Font", "Color", "Tone", "Rect",
                              "Table", "Graphics", "Input", "Audio" };
    for (int i = 0; i < 14; i++) {
        char buf[512];
        sprintf(buf, "%s.instance_methods(false).sort.join(' ')", klasses[i]);
        printf("%-10s inst: %s\n", klasses[i], g_getstr(buf));
        sprintf(buf, "%s.methods(false).sort.join(' ')", klasses[i]);
        printf("%-10s sing: %s\n", "", g_getstr(buf));
    }

    puts("\n===== 3. 方法 arity =====");
    for (int i = 0; i < 14; i++) {
        char buf[1024];
        sprintf(buf, "%s.instance_methods(false).sort.map{|m| \"#{m}:#{%s.instance_method(m).arity}\"}.join(' ')",
                klasses[i], klasses[i]);
        printf("%-10s %s\n", klasses[i], g_getstr(buf));
    }

    puts("\n===== 4. Bitmap 对象内部 =====");
    run("$b = Bitmap.new(32, 24)");
    show("Bitmap.new(32,24).width", "$b.width");
    show("height", "$b.height");
    show("rect", "$b.rect.inspect");
    show("class", "$b.class.to_s");
    show("instance_variables", "$b.instance_variables.inspect");
    show("disposed?", "$b.disposed?");
    show("respond_to?(:freeze)", "$b.respond_to?(:freeze).to_s");

    puts("\n===== 5. 常量表 =====");
    show("Graphics constants", "Graphics.constants.sort.join(' ')");
    show("Input constants", "Input.constants.sort.join(' ')");
    show("Font constants", "Font.constants.sort.join(' ')");
    show("Font defaults", "Font.default_name.inspect + ' ' + Font.default_size.to_s + ' ' + Font.default_bold.to_s + ' ' + Font.default_italic.to_s + ' ' + Font.default_shadow.to_s");
    show("Color default", "Font.default_color.inspect");

    puts("\n===== 6. Graphics 状态 =====");
    show("frame_rate", "Graphics.frame_rate.to_s");
    show("frame_count", "Graphics.frame_count.to_s");
    show("brightness", "Graphics.brightness.to_s");
    show("width", "Graphics.width.to_s rescue 'N/A'");
    show("height", "Graphics.height.to_s rescue 'N/A'");

    puts("\n===== 7. 像素格式探测 =====");
    run("$b2 = Bitmap.new(2,2); $b2.set_pixel(0,0,Color.new(255,0,0,255)); $b2.set_pixel(1,0,Color.new(0,255,0,128))");
    show("pixel(0,0)", "$b2.get_pixel(0,0).inspect");
    show("pixel(1,0)", "$b2.get_pixel(1,0).inspect");
    show("Color inspect", "Color.new(1,2,3,4).inspect");
    show("Tone inspect", "Tone.new(1,2,3,4).inspect");
    show("Rect inspect", "Rect.new(1,2,3,4).inspect");
    show("Table inspect", "t=Table.new(2,3); t.inspect");
    show("Table size", "t=Table.new(2,3,4); [t.xsize,t.ysize,t.zsize].inspect");

    puts("\n===== 8. 内置脚本可见性 =====");
    show("RPG defined?", "defined?(RPG).inspect");
    show("RPG::BGM", "defined?(RPG::BGM).inspect");
    show("load_data", "defined?(load_data).inspect");
    show("$RGSS_SCRIPTS", "$RGSS_SCRIPTS.inspect");
    return 0;
}
