/* gen_shape.c — shape oracle 探针生成器（保留原版游戏脚本, 附加探针线程）
 * 协议: 探针线程等地图就绪 → 写 ready.txt → 等 go<k> → 改 16 格邻域配置 → 等 8 帧
 *       → 写 done<k>；shapeshot 外部: 等 ready → go0 → 等 done0 → 截图 → go1 ...
 * 用法: gen_shape.exe <rebuild.dll>   (cwd 需含 Data/ 且为原版游戏脚本) */
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
      "Thread.new {\n"
      "  begin\n"
      "    sleep 0.2 while ($game_map.nil? rescue true)\n"
      "    sleep 1.0\n"
      "    $game_map.events.values.each { |e| e.transparent = true } rescue nil\n"
      "    f = File.open('ready.txt','w'); f.close\n"
      "    W = 3377\n"
      "    L = 1611\n"
      "    d = $game_map.data\n"
      "    0.upto(15) { |k|\n"
      "      sleep 0.1 until File.exist?('go' + k.to_s + '.txt')\n"
      "      0.upto(15) { |i|\n"
      "        c = k * 16 + i\n"
      "        orth = c & 15\n"
      "        diag = (c >> 4) & 15\n"
      "        cx = 64 + 3 * (i % 4)\n"
      "        cy = 64 + 3 * (i / 4)\n"
      "        d[cx, cy, 0] = W\n"
      "        d[cx, cy, 1] = 0\n"
      "        d[cx, cy, 2] = 0\n"
      "        d[cx, cy - 1, 0] = (orth & 1) == 1 ? W : L\n"
      "        d[cx, cy + 1, 0] = (orth & 2) == 2 ? W : L\n"
      "        d[cx - 1, cy, 0] = (orth & 4) == 4 ? W : L\n"
      "        d[cx + 1, cy, 0] = (orth & 8) == 8 ? W : L\n"
      "        d[cx - 1, cy - 1, 0] = (diag & 1) == 1 ? W : L\n"
      "        d[cx + 1, cy - 1, 0] = (diag & 2) == 2 ? W : L\n"
      "        d[cx - 1, cy + 1, 0] = (diag & 4) == 4 ? W : L\n"
      "        d[cx + 1, cy + 1, 0] = (diag & 8) == 8 ? W : L\n"
      "      }\n"
      "      $game_player.moveto(68, 68)\n"
      "      Graphics.wait(8)\n"
      "      f = File.open('done' + k.to_s + '.txt', 'w'); f.close\n"
      "    }\n"
      "    while true; sleep 1; end\n"
      "  rescue Exception => e\n"
      "    f = File.open('probe_err.txt','w'); f.puts e.class.to_s + ' ' + e.message.to_s; f.close\n"
      "  end\n"
      "}\n"
      "EOS\n"
      "arr = File.open('orig_scripts.rvdata','rb') { |f| Marshal.load(f.read) }\n"
      "arr << [999, 'shapeprobe', Zlib::Deflate.deflate(code)]\n"
      "File.open('probe_scripts.rvdata','wb') { |f| f.write Marshal.dump(arr) }\n"
      "File.delete('Data/Scripts.rvdata') rescue nil\n"
      "File.rename('Data/Scripts.rvdata.new','Data/Scripts.rvdata')\n"
      "'GENOK';"
      "rescue Exception => e; 'GENERR ' + e.class.to_s + ' ' + e.message.to_s; end");
    printf("gen => %d\n", rc);
    return rc;
}
