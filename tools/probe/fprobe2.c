#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
typedef int (*FnInit)(int); typedef int (*FnEval)(const char *); typedef int (*FnGetStr)(const char *);
static DWORD WINAPI run_main(LPVOID p){ typedef int (*F)(int,int,int); F m=(F)((void**)p)[0]; m((int)(DWORD_PTR)((void**)p)[1],0,0); return 0; }
int main(int argc,char**argv){
  HMODULE h; FnInit init; FnEval ev; FnGetStr gs; char code[65536]; FILE*f; long n;
  h=LoadLibraryA(argv[1]);
  init=(FnInit)GetProcAddress(h,"RGSSInitialize2");
  ev=(FnEval)GetProcAddress(h,"RGSSEval"); gs=(FnGetStr)GetProcAddress(h,"RGSSGetStringUTF8");
  init(0);
  { void*fp[2]; HANDLE th; HWND hw;
    typedef int (*F)(int,int,int);
    hw = CreateWindowA('STATIC','p',WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,544,416,NULL,NULL,NULL,NULL);
    fp[0] = GetProcAddress(h,'RGSSGameMain'); fp[1] = hw;
    th = CreateThread(NULL,0,run_main,fp,0,NULL);
    Sleep(7000); }
  ev("$out=[]; begin;"
     " f=Font.new; "
     " $out << 'shadow='+f.shadow.inspect;"
     " $out << 'bold='+f.bold.inspect+' italic='+f.italic.inspect;"
     " $out << 'size='+f.size.inspect;"
     " $out << 'color='+f.color.inspect;"
     " $out << 'name='+f.name.inspect;"
     " $out << 'Font.default_shadow='+Font.default_shadow.inspect;"
     " $out << 'Font.default_size='+Font.default_size.inspect;"
     " $out << 'Font.default_color='+Font.default_color.inspect;"
     " $out << 'Font.default_name='+Font.default_name.inspect;"
     " rescue Exception=>e; $out << 'EXC '+e.class.to_s+': '+e.message; end;"
     "File.open('fontdump.txt','wb'){|f| f.puts($out)}");
  Sleep(1200);
  f=fopen("fontdump.txt","rb"); if(f){ n=fread(code,1,sizeof(code)-1,f); code[n]=0; fclose(f); printf("%s\n",code);} else printf("no file\n");
  return 0;
}
