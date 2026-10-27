#include <windows.h>
#include <stdio.h>
typedef int (*FnInit)(int);
typedef const char* (*FnStr)(const char*);
static FnStr gs;
static void one(const char*code){
  char b[2048]; sprintf(b,"$__t = begin; (%s).inspect; rescue => e; \"ERR\" end", code);
  int r = ((int(*)(const char*))0) ? 0 : 0;
  gs(b);
  printf("  [%s] -> %s\n", code, gs("$__t"));
}
int main(int c,char**v){
  HMODULE h=LoadLibraryA(v[1]);
  ((FnInit)GetProcAddress(h,"RGSSInitialize2"))(0);
  gs=(FnStr)GetProcAddress(h,"RGSSGetStringUTF8");
  const char* cases[]={"1+1","RUBY_VERSION","(1..10).inject{|s,x|s+x}","'a'+'b'","def f(n);n*2;end;f(21)","'hoge'.sub(/o/,'0')","Marshal.dump(42).unpack('H*')[0]"};
  for(int i=0;i<7;i++){ printf("case %d:",i); fflush(stdout); one(cases[i]); }
  return 0;
}
