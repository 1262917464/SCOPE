/* probe_input2.c — 深挖 Input 模块的真实状态 */
#include <windows.h>
#include <stdio.h>

typedef int (*FnInit)(int);
typedef int (*FnEval)(const char *);
typedef int (*FnGetStr)(const char *);

static FnEval ev;
static FnGetStr gs;

static void dump(const char *label, const char *expr)
{
    char code[4096];
    sprintf(code,
        "$__r = begin; %s; rescue Exception => e; 'EXC:'+e.class.to_s+':'+e.message; end",
        expr);
    ev(code);
    printf("%-34s => %s\n", label, gs("$__r.inspect"));
}

int main(int argc, char **argv)
{
    HMODULE h;
    FnInit init;

    if (argc < 2) return 2;
    h = LoadLibraryA(argv[1]);
    if (!h) { printf("LoadLibrary fail %lu\n", GetLastError()); return 1; }
    init = (FnInit)GetProcAddress(h, "RGSSInitialize2");
    ev   = (FnEval)GetProcAddress(h, "RGSSEval");
    gs   = (FnGetStr)GetProcAddress(h, "RGSSGetStringUTF8");
    if (!init || !ev || !gs) { printf("missing exports\n"); return 1; }
    init(0);

    puts("=== after RGSSInitialize2 (phase A only) ===");
    dump("defined?(Input)",        "defined?(Input)");
    dump("Input.class",            "Input.class.to_s");
    dump("Input.constants",        "Input.constants.sort.join(',')");
    dump("Input.methods(false)",   "Input.methods(false).sort.join(',')");
    dump("defined?(Graphics)",     "defined?(Graphics)");
    dump("Graphics.methods(false)","Graphics.methods(false).sort.join(',')");

    puts("\n=== after Graphics.update (phase B trigger) ===");
    dump("Graphics.update",        "Graphics.update.class.to_s");
    dump("Input.constants",        "Input.constants.sort.join(',')");
    dump("Input.methods(false)",   "Input.methods(false).sort.join(',')");
    dump("Input::DOWN",            "Input::DOWN");
    dump("Input::LEFT",            "Input::LEFT");
    dump("Input::RIGHT",           "Input::RIGHT");
    dump("Input::UP",              "Input::UP");
    dump("Input::A",               "Input::A");
    dump("Input::B",               "Input::B");
    dump("Input::C",               "Input::C");
    dump("Input::X",               "Input::X");
    dump("Input::Y",               "Input::Y");
    dump("Input::Z",               "Input::Z");
    dump("Input::L",               "Input::L");
    dump("Input::R",               "Input::R");
    dump("Input::SHIFT",           "Input::SHIFT");
    dump("Input::CTRL",            "Input::CTRL");
    dump("Input::ALT",             "Input::ALT");
    dump("Input::F5",              "Input::F5");
    dump("Input::F6",              "Input::F6");
    dump("Input::F7",              "Input::F7");
    dump("Input::F8",              "Input::F8");
    dump("Input::F9",              "Input::F9");
    dump("Input.dir4",             "Input.dir4");
    dump("Input.dir8",             "Input.dir8");
    dump("Input.press?(2)",        "Input.press?(2)");
    dump("Input.trigger?(2)",      "Input.trigger?(2)");

    FreeLibrary(h);
    return 0;
}
