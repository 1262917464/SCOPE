/* gen_blt_rvdata.c — 用重建 DLL (进程内 Ruby) 生成 blt 密集采样 Scripts.rvdata
 * 用法: gen_blt_rvdata.exe <rebuild.dll>   (cwd 需含 Data/)
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
          Sleep(4000);
          if (hw) PostMessageA(hw, WM_CLOSE, 0, 0);
          WaitForSingleObject(th, 10000);
          CloseHandle(th);
          Sleep(500); }
    }

    rc = ev(
      "begin;"
      "code = <<'EOS'.dup\n"
      "$L = []\n"
      "begin\n"
      "  File.delete('go.txt') rescue nil\n"
      "  while !File.exist?('go.txt'); sleep 0.2; end\n"
      "  dumpc = lambda { |c| '(' + c.red.to_i.to_s + ',' + c.green.to_i.to_s + ',' + c.blue.to_i.to_s + ',' + c.alpha.to_i.to_s + ')' }\n"
      "  d = Bitmap.new(20,1); d.fill_rect(0,0,20,1,Color.new(100,150,200,255))\n"
      "  sa = [0,1,2,4,8,16,32,48,64,96,128,160,192,224,240,250,253,254,255]\n"
      "  sA = Bitmap.new(20,1)\n"
      "  sa.each_with_index { |a,i| sA.set_pixel(i,0,Color.new(255,10,60,a)) }\n"
      "  d.blt(0,0,sA,sA.rect,255)\n"
      "  $L << 'A ' + (0...19).map { |x| dumpc.call(d.get_pixel(x,0)) }.join(' ')\n"
      "  ops = [1,2,4,8,16,32,48,64,96,128,160,192,224,254,255]\n"
      "  sB = Bitmap.new(16,1); (0...15).each { |i| sB.set_pixel(i,0,Color.new(255,10,60,128)) }\n"
      "  ops.each_with_index { |op,i| t = Bitmap.new(1,1); t.fill_rect(0,0,1,1,Color.new(100,150,200,255)); t.blt(0,0,sB,Rect.new(i,0,1,1),op); $L << 'B op=' + op.to_s + ' ' + dumpc.call(t.get_pixel(0,0)) }\n"
      "  ops.each_with_index { |op,i| t = Bitmap.new(1,1); t.fill_rect(0,0,1,1,Color.new(100,150,200,0)); t.blt(0,0,sB,Rect.new(i,0,1,1),op); $L << 'C op=' + op.to_s + ' ' + dumpc.call(t.get_pixel(0,0)) }\n"
      "  [0,32,64,96,128,192,255].each_with_index { |da,i| t = Bitmap.new(1,1); t.fill_rect(0,0,1,1,Color.new(100,150,200,da)); t.blt(0,0,sB,Rect.new(4,0,1,1),128); $L << 'D da=' + da.to_s + ' ' + dumpc.call(t.get_pixel(0,0)) }\n"
      "  (5..7).each { |dw| sd = Bitmap.new(dw,1); sd.stretch_blt(sd.rect,sA,Rect.new(0,0,8,1),255); $L << 'E dw=' + dw.to_s + ' ' + (0...dw).map { |x| dumpc.call(sd.get_pixel(x,0)) }.join(' ') }\n"
      "  [8,12].each { |dw| sd = Bitmap.new(dw,1); sd.stretch_blt(sd.rect,sA,Rect.new(0,0,4,1),255); $L << 'E2 dw=' + dw.to_s + ' ' + (0...dw).map { |x| dumpc.call(sd.get_pixel(x,0)) }.join(' ') }\n"
      "  # G: da full sweep 1..255, src=(255,10,60,128) op=128\n"
      "  gd = (1..255).map { |da| t = Bitmap.new(1,1); t.set_pixel(0,0,Color.new(100,150,200,da)); t.blt(0,0,sB,Rect.new(4,0,1,1),128); dumpc.call(t.get_pixel(0,0)) }\n"
      "  $L << 'G ' + gd.join(' ')\n"
      "  # G2: da full sweep, op=200\n"
      "  g2 = (1..255).map { |da| t = Bitmap.new(1,1); t.set_pixel(0,0,Color.new(100,150,200,da)); t.blt(0,0,sB,Rect.new(4,0,1,1),200); dumpc.call(t.get_pixel(0,0)) }\n"
      "  $L << 'G2 ' + g2.join(' ')\n"
      "  # F: isolate fill_rect vs blt - dst via set_pixel, read back before blt\n"
      "  f1 = Bitmap.new(1,1); f1.set_pixel(0,0,Color.new(100,150,200,32))\n"
      "  $L << 'F dst_before ' + dumpc.call(f1.get_pixel(0,0))\n"
      "  f1.blt(0,0,sB,Rect.new(4,0,1,1),128)\n"
      "  $L << 'F setpx da=32 ' + dumpc.call(f1.get_pixel(0,0))\n"
      "  f2 = Bitmap.new(1,1); f2.set_pixel(0,0,Color.new(100,150,200,64))\n"
      "  f2.blt(0,0,sB,Rect.new(4,0,1,1),128)\n"
      "  $L << 'F setpx da=64 ' + dumpc.call(f2.get_pixel(0,0))\n"
      "  f3 = Bitmap.new(1,1); f3.set_pixel(0,0,Color.new(100,150,200,96))\n"
      "  f3.blt(0,0,sB,Rect.new(4,0,1,1),128)\n"
      "  $L << 'F setpx da=96 ' + dumpc.call(f3.get_pixel(0,0))\n"
      "  f4 = Bitmap.new(1,1); f4.fill_rect(0,0,1,1,Color.new(100,150,200,32))\n"
      "  $L << 'F fill_before ' + dumpc.call(f4.get_pixel(0,0))\n"
      "  f4.blt(0,0,sB,Rect.new(4,0,1,1),128)\n"
      "  $L << 'F fillpx da=32 ' + dumpc.call(f4.get_pixel(0,0))\n"
      "  sd = Bitmap.new(5,1); sd.stretch_blt(sd.rect,sA,Rect.new(0,0,8,1),128); $L << 'E3 8to5 op128 ' + (0...5).map { |x| dumpc.call(sd.get_pixel(x,0)) }.join(' ')\n"
      "rescue Exception => e\n"
      "  $L << 'EXC ' + e.class.to_s + ' ' + e.message.to_s\n"
      "end\n"
      "f = File.open('blt_result.txt','w')\n"
      "$L.each { |s| f.puts s }\n"
      "f.close\n"
      "exit\n"
      "EOS\n"
      "arr = [[1, 'bltcheck', Zlib::Deflate.deflate(code)]]\n"
      "File.open('Data/Scripts.rvdata','wb') { |f| f.write Marshal.dump(arr) }\n"
      "'GENOK';"
      "rescue Exception => e; 'GENERR ' + e.class.to_s + ' ' + e.message.to_s; end");
    printf("gen => %d\n", rc);
    return rc;
}
