/* gen_dupcheck.c — 生成 dup/方法表对拍 rvdata（Game.exe 两端跑）
 * 产物: dup_progress.txt（追加式进度，崩溃可定位）/ dup_result.txt（完成后一次性写）
 * 用法: gen_dupcheck.exe <rebuild.dll>   (cwd 需含 Data/)
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
    int rc;

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
      "$L = []\n"
      "File.delete('dup_progress.txt') rescue nil\n"
      "def w(s)\n"
      "  $L << s\n"
      "  File.open('dup_progress.txt','a') { |f| f.puts s }\n"
      "end\n"
      "def t(nm)\n"
      "  w nm + ' ...'\n"
      "  begin\n"
      "    r = yield\n"
      "    w nm + ' => OK(' + (r.nil? ? 'nil' : (r.is_a?(Fixnum) || r.is_a?(Float) || r.is_a?(TrueClass) || r.is_a?(FalseClass) ? r.to_s : r.class.to_s)) + ')'\n"
      "  rescue Exception => e\n"
      "    w nm + ' => ' + e.class.to_s + ': ' + e.message.to_s\n"
      "  end\n"
      "end\n"
      "File.delete('go.txt') rescue nil\n"
      "while !File.exist?('go.txt'); sleep 0.2; end\n"
      "w 'Ruby ' + RUBY_VERSION\n"
      "t('Tilemap.dup')    { Tilemap.new.dup }\n"
      "t('Tilemap.clone')  { Tilemap.new.clone }\n"
      "t('Sprite.clone')   { Sprite.new.clone }\n"
      "t('Sprite.dup')     { Sprite.new.dup }\n"
      "t('Plane.clone')    { Plane.new.clone }\n"
      "t('Plane.dup')      { Plane.new.dup }\n"
      "t('Window.clone')   { Window.new.clone }\n"
      "t('Window.dup')     { Window.new.dup }\n"
      "t('Viewport.new4')  { Viewport.new(0,0,10,10) }\n"
      "t('Viewport.clone') { Viewport.new(0,0,10,10).clone }\n"
      "t('Viewport.dup')   { Viewport.new(0,0,10,10).dup }\n"
      "t('Bitmap.dup')     { Bitmap.new(4,4).dup }\n"
      "t('Bitmap.clone')   { Bitmap.new(4,4).clone }\n"
      "t('Font.dup')       { Font.new.dup }\n"
      "t('Rect.dup')       { Rect.new(1,2,3,4).dup }\n"
      "t('Color.dup')      { Color.new(1,2,3,4).dup }\n"
      "t('Tone.dup')       { Tone.new(1,2,3,4).dup }\n"
      "t('Table.dup')      { Table.new(2,2).dup }\n"
      "t('Plane.viewport0')    { Plane.new.viewport }\n"
      "t('Plane.viewport1')    { Plane.new.viewport(nil) }\n"
      "t('Plane.viewport_set') { (Plane.new.viewport = Viewport.new(0,0,10,10)); :set }\n"
      "t('Viewport.disposed?') { Viewport.new(0,0,10,10).disposed? }\n"
      "t('Tilemap.bitmaps_set'){ (Tilemap.new.bitmaps = nil); :set }\n"
      "t('Sprite.width')   { Sprite.new.width }\n"
      "t('Sprite.height')  { Sprite.new.height }\n"
      "t('Window.zoom_x?') { Window.new.respond_to?(:zoom_x) }\n"
      "sw = Sprite.new\n"
      "t('Sprite.width_nobmp') { sw.width }\n"
      "sw.bitmap = Bitmap.new(30,40)\n"
      "t('Sprite.width_bmp')  { sw.width }\n"
      "t('Sprite.height_bmp') { sw.height }\n"
      "sw.src_rect = Rect.new(2,3,10,5)\n"
      "t('Sprite.width_srcrect') { sw.width }\n"
      "sz = Sprite.new\n"
      "sz.bitmap = Bitmap.new(30,40)\n"
      "sz.zoom_x = 2.0; sz.zoom_y = 3.0\n"
      "t('Sprite.width_zoom') { sz.width }\n"
      "t('Sprite.height_zoom') { sz.height }\n"
      "sx = Sprite.new\n"
      "sx.bitmap = Bitmap.new(30,40)\n"
      "sx.x = 7; sx.y = 9\n"
      "t('Sprite.width_xy') { sx.width }\n"
      "sv = Sprite.new\n"
      "sv.bitmap = Bitmap.new(30,40)\n"
      "sv.viewport = Viewport.new(0,0,50,60)\n"
      "t('Sprite.width_vp') { sv.width }\n"
      "%w(Sprite Window Plane Viewport Tilemap Bitmap Rect Color Tone Font Table).each { |cn|\n"
      "  c = Object.const_get(cn) rescue next\n"
      "  w 'M ' + cn + ' ' + (c.instance_methods(false).map { |s| s.to_s }.sort.join(','))\n"
      "}\n"
      "ba = (Tilemap::BitmapArray rescue nil)\n"
      "w 'M Tilemap::BitmapArray ' + (ba ? ba.instance_methods(false).map { |s| s.to_s }.sort.join(',') : 'NOC')\n"
      "w 'ALLDONE'\n"
      "f = File.open('dup_result.txt','w')\n"
      "$L.each { |s| f.puts s }\n"
      "f.close\n"
      "exit\n"
      "EOS\n"
      "arr = [[1, 'dupcheck', Zlib::Deflate.deflate(code)]]\n"
      "File.open('Data/Scripts.rvdata','wb') { |f| f.write Marshal.dump(arr) }\n"
      "'GENOK';"
      "rescue Exception => e; 'GENERR ' + e.class.to_s + ' ' + e.message.to_s; end");
    printf("gen => %d\n", rc);
    return rc;
}
