/* gen_mapdump.c — 读 Project3 Map001 全量 tile id 导出（定位接缝格的层/id）
 * 用法: gen_mapdump.exe <rebuild.dll>   (cwd 需含 Data/)  */
#include <windows.h>
#include <stdio.h>

typedef int (*FnInit)(int);
typedef int (*FnEval)(const char *);

static DWORD WINAPI runner(LPVOID p)
{
    typedef int (*F)(int, int, int);
    F m = (F)((void **)p)[0];
    m((int)(DWORD_PTR)((void **)p)[1], 0, 0);
    return 0;
}

int main(int argc, char **argv)
{
    HMODULE h; FnInit init; FnEval ev; int rc;
    if (argc < 2) return 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    h = LoadLibraryA(argv[1]);
    if (!h) { printf("load fail\n"); return 1; }
    init = (FnInit)GetProcAddress(h, "RGSSInitialize2");
    ev   = (FnEval)GetProcAddress(h, "RGSSEval");
    init(0);
    {
        void *fp[2];
        HWND hw = CreateWindowA("STATIC", "g", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                0, 0, 544, 416, NULL, NULL, NULL, NULL);
        fp[0] = (void *)GetProcAddress(h, "RGSSGameMain");
        fp[1] = (void *)hw;
        { HANDLE th = CreateThread(NULL, 0, runner, fp, 0, NULL);
          Sleep(3000);
          if (hw) PostMessageA(hw, WM_CLOSE, 0, 0);
          WaitForSingleObject(th, 10000);
          CloseHandle(th);
          Sleep(500); }
    }
    rc = ev(
      "begin;"
      "code = <<'EOS'.dup\n"
      "File.delete('mapdump.txt') rescue nil\n"
      "m = load_data('Data/Map001.rvdata')\n"
      "t = m.data\n"
      "f = File.open('mapdump.txt','w')\n"
      "f.puts [t.xsize, t.ysize, t.zsize].join(',')\n"
      "(0...t.xsize).each { |x|\n"
      "  (0...t.ysize).each { |y|\n"
      "    (0...t.zsize).each { |z|\n"
      "      v = t[x,y,z]\n"
      "      f.puts x.to_s + ',' + y.to_s + ',' + z.to_s + ',' + v.to_s if v != 0\n"
      "    }\n"
      "  }\n"
      "}\n"
      "f.close\n"
      "exit\n"
      "EOS\n"
      "arr = [[1, 'mapdump', Zlib::Deflate.deflate(code)]]\n"
      "File.open('Data/Scripts.rvdata','wb') { |f| f.write Marshal.dump(arr) }\n"
      "'GENOK';"
      "rescue Exception => e; 'GENERR ' + e.class.to_s + ' ' + e.message.to_s; end");
    printf("gen => %d\n", rc);
    return rc;
}
