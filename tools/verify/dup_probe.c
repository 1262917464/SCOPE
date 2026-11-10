/* dup_probe.c — 进程内直接 RGSSEval 测试用例，stdout 打结果
 * 用法: dup_probe.exe <dll>
 */
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
    HMODULE h;
    FnInit init;
    FnEval ev;
    void *fp[2];
    HWND hw;
    HANDLE th;
    int rc;

    if (argc < 2) return 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    h = LoadLibraryA(argv[1]);
    if (!h) { printf("load fail\n"); return 1; }
    init = (FnInit)GetProcAddress(h, "RGSSInitialize2");
    ev   = (FnEval)GetProcAddress(h, "RGSSEval");
    init(0);
    hw = CreateWindowA("STATIC", "g", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                       0, 0, 544, 416, NULL, NULL, NULL, NULL);
    fp[0] = (void *)GetProcAddress(h, "RGSSGameMain");
    fp[1] = (void *)hw;
    th = CreateThread(NULL, 0, runner, fp, 0, NULL);
    Sleep(1500);

    rc = ev(
      "File.delete('probe_out.txt') rescue nil\n"
      "def w2(s)\n"
      "  File.open('probe_out.txt','a') { |f| f.puts s }\n"
      "end\n"
      "w2 'start Ruby ' + RUBY_VERSION\n"
      "def probe(nm)\n"
      "  w2 nm + ' ...'\n"
      "  begin\n"
      "    x = yield\n"
      "    w2 nm + ' => OK(' + (x.nil? ? 'nil' : x.class.to_s) + ')'\n"
      "  rescue Exception => e\n"
      "    w2 nm + ' => ' + e.class.to_s + ': ' + e.message.to_s\n"
      "  end\n"
      "end\n"
      "probe('Tilemap.dup')   { Tilemap.new.dup }\n"
      "probe('Tilemap.clone') { Tilemap.new.clone }\n"
      "probe('Sprite.clone')  { Sprite.new.clone }\n"
      "probe('Sprite.dup')    { Sprite.new.dup }\n"
      "probe('Plane.clone')   { Plane.new.clone }\n"
      "probe('Window.clone')  { Window.new.clone }\n"
      "probe('Window.dup')    { Window.new.dup }\n"
      "probe('Viewport.new4') { Viewport.new(0,0,10,10) }\n"
      "probe('Viewport.clone'){ Viewport.new(0,0,10,10).clone }\n"
      "probe('Viewport.dup')  { Viewport.new(0,0,10,10).dup }\n"
      "probe('Bitmap.dup')    { Bitmap.new(4,4).dup }\n"
      "probe('Bitmap.clone')  { Bitmap.new(4,4).clone }\n"
      "probe('Font.dup')      { Font.new.dup }\n"
      "probe('Rect.dup')      { Rect.new(1,2,3,4).dup }\n"
      "probe('Color.dup')     { Color.new(1,2,3,4).dup }\n"
      "probe('Tone.dup')      { Tone.new(1,2,3,4).dup }\n"
      "probe('Table.dup')     { Table.new(2,2).dup }\n"
      "probe('Plane.viewport0') { Plane.new.viewport }\n"
      "probe('Plane.viewport1') { Plane.new.viewport(nil) }\n"
      "probe('Plane.viewport=') { (Plane.new.viewport = Viewport.new(0,0,10,10)); :set }\n"
      "probe('Viewport.disposed?') { Viewport.new(0,0,10,10).disposed? }\n"
      "probe('Tilemap.bitmaps=') { (Tilemap.new.bitmaps = nil); :set }\n"
      "probe('Sprite.width')   { Sprite.new.width }\n"
      "probe('Sprite.height')  { Sprite.new.height }\n"
      "probe('Window.zoom_x?') { Window.new.respond_to?(:zoom_x) }\n"
      "%w(Sprite Window Plane Viewport Tilemap Bitmap Rect Color Tone Font Table).each { |cn|\n"
      "  c = Object.const_get(cn) rescue next\n"
      "  w2 'M ' + cn + ' ' + (c.instance_methods(false).map { |s| s.to_s }.sort.join(','))\n"
      "}\n"
      "ba = (Tilemap::BitmapArray rescue nil)\n"
      "w2 'M Tilemap::BitmapArray ' + (ba ? ba.instance_methods(false).map { |s| s.to_s }.sort.join(',') : 'NOC')\n"
      "w2 'ALLDONE'\n"
      "'PROBEOK';");

    if (hw) PostMessageA(hw, WM_CLOSE, 0, 0);
    WaitForSingleObject(th, 5000);
    printf("rc=%d\n", rc);
    return 0;
}
