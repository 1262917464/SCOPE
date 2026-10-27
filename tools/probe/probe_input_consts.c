/* probe_input_consts.c — 用原版 DLL 实测 Input 常量数值
 * 用法: probe_input_consts.exe <RGSS202E.dll 路径>
 */
#include <windows.h>
#include <stdio.h>

typedef int (*FnInit)(int);
typedef int (*FnEval)(const char *);
typedef int (*FnGetInt)(const char *);

int main(int argc, char **argv)
{
    HMODULE h;
    FnInit init;
    FnEval ev;
    FnGetInt gi;
    const char *names[] = {
        "DOWN", "LEFT", "RIGHT", "UP",
        "A", "B", "C", "X", "Y", "Z", "L", "R",
        "SHIFT", "CTRL", "ALT", "F5", "F6", "F7", "F8", "F9",
        NULL
    };
    int i;

    if (argc < 2) { printf("usage: %s <dll>\n", argv[0]); return 2; }
    h = LoadLibraryA(argv[1]);
    if (!h) { printf("LoadLibrary failed: %lu\n", GetLastError()); return 1; }
    init = (FnInit)GetProcAddress(h, "RGSSInitialize2");
    ev   = (FnEval)GetProcAddress(h, "RGSSEval");
    gi   = (FnGetInt)GetProcAddress(h, "RGSSGetInt");
    if (!init || !ev || !gi) { printf("missing exports\n"); return 1; }

    init(0);
    /* 触发第二阶段注册（图形类包含 Input） */
    ev("Graphics.update");

    printf("--- Input constants ---\n");
    for (i = 0; names[i]; i++) {
        char code[128];
        sprintf(code, "Input::%s", names[i]);
        printf("%-6s = %d\n", names[i], gi(code));
    }
    printf("--- Input methods ---\n");
    {
        char buf[4096];
        ev("$__r = Input.methods(false).sort.join(' ')");
        /* 用 eval 把结果写进一个变量，再逐字符读不出，直接打印方法列表 */
        ev("puts_stub = 0");
        (void)buf;
    }
    ev("File.open('input_probe.txt','wb'){|f| "
       "f.write(\"consts={\\n\"); "
       "['DOWN','LEFT','RIGHT','UP','A','B','C','X','Y','Z','L','R','SHIFT','CTRL','ALT','F5','F6','F7','F8','F9'].each{|n| "
       "  v = Input.const_get(n) rescue nil; "
       "  f.write(\"  #{n}=#{v.inspect},\\n\") }; "
       "f.write(\"}\\n\"); "
       "f.write(\"methods=#{Input.methods(false).sort.join(',')}\\n\"); "
       "}");
    printf("wrote input_probe.txt\n");
    FreeLibrary(h);
    return 0;
}
