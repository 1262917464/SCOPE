#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
typedef int (*FnInit)(int); typedef int (*FnEval)(const char *); typedef int (*FnGetStr)(const char *);
static DWORD WINAPI runner(LPVOID p){ typedef int (*F)(int,int,int); F m=(F)((void**)p)[0]; m((int)(DWORD_PTR)((void**)p)[1],0,0); return 0; }
int main(int argc,char**argv){
  HMODULE h; FnInit init; FnEval ev; FnGetStr gs; void*fp[2]; HANDLE th; char code[65536]; FILE*f; long n; HWND hw;
  h=LoadLibraryA(argv[1]);
  init=(FnInit)GetProcAddress(h,"RGSSInitialize2");
  ev=(FnEval)GetProcAddress(h,"RGSSEval"); gs=(FnGetStr)GetProcAddress(h,"RGSSGetStringUTF8");
  init(0);
  hw=CreateWindowA("STATIC","p",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,544,416,NULL,NULL,NULL,NULL);
  fp[0]=GetProcAddress(h,"RGSSGameMain"); fp[1]=hw;
  th=CreateThread(NULL,0,runner,fp,0,NULL);
  Sleep(6000);
  ev("$out=[]; "
     "begin;"
     " m=load_data('Data/Map001.rvdata'); d=m.data; "
     " $out << 'dims='+d.xsize.to_s+','+d.ysize.to_s+','+d.zsize.to_s; "
     " ids=Hash.new(0); "
     " d.xsize.times{|x| d.ysize.times{|y| d.zsize.times{|z| t=d[x,y,z]; ids[t]+=1 if t!=0 }}}; "
     " $out << 'unique='+ids.size.to_s; "
     " ks=ids.keys.sort; $out << 'min='+(ks.first||0).to_s+' max='+(ks.last||0).to_s; "
     " $out << 'ids='+(ks.first(60).join(',')); "
     " $out << 'top='+(ids.sort_by{|k,v|-v}.first(12).map{|k,v| k.to_s+':'+v.to_s}.join(',')); "
     " rescue Exception=>e; $out << 'EXC '+e.class.to_s+': '+e.message; end; "
     "File.open('mapdata.txt','wb'){|f| f.puts($out)}");
  Sleep(1500);
  f=fopen("mapdata.txt","rb"); if(f){ n=fread(code,1,sizeof(code)-1,f); code[n]=0; fclose(f); printf("%s\n",code);} else printf("no file\n");
  TerminateProcess(GetCurrentProcess(),0); return 0;
}
