#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
typedef int (*FnInit)(int); typedef int (*FnEval)(const char *); typedef int (*FnGetStr)(const char *);
int main(int argc,char**argv){
  HMODULE h; FnInit init; FnEval ev; FnGetStr gs; char code[65536]; FILE*f; long n;
  h=LoadLibraryA(argv[1]);
  init=(FnInit)GetProcAddress(h,"RGSSInitialize2");
  ev=(FnEval)GetProcAddress(h,"RGSSEval"); gs=(FnGetStr)GetProcAddress(h,"RGSSGetStringUTF8");
  init(0);
  ev("$out=[]; begin;"
     " t=Table.new(5); $out << '1D dump='+t._dump(0).unpack('N*').inspect+' len='+t._dump(0).length.to_s;"
     " t=Table.new(3,4); $out << '2D dump='+t._dump(0).unpack('N*').inspect+' len='+t._dump(0).length.to_s;"
     " t=Table.new(3,4,5); $out << '3D dump='+t._dump(0).unpack('N*').inspect+' len='+t._dump(0).length.to_s;"
     " t=Table.new(100,100,4); $out << 'map dump='+t._dump(0).unpack('N*').inspect+' len='+t._dump(0).length.to_s;"
     " t=Table.new(3,4,5); t[1,2,3]=42; $out << 'set/get='+t[1,2,3].to_s;"
     " t2=Table._load(t._dump(0)); $out << 'roundtrip='+t2[1,2,3].to_s;"
     " rescue Exception=>e; $out << 'EXC '+e.class.to_s+': '+e.message; end;"
     "File.open('tabledump.txt','wb'){|f| f.puts($out)}");
  Sleep(1200);
  f=fopen("tabledump.txt","rb"); if(f){ n=fread(code,1,sizeof(code)-1,f); code[n]=0; fclose(f); printf("%s\n",code);} else printf("no file\n");
  return 0;
}
