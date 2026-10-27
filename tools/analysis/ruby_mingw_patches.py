#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""ruby_mingw_patches.py — Ruby 1.8.1 → mingw32/gcc12 兼容补丁(幂等)
用法: python ruby_mingw_patches.py <ruby-build-dir>
"""
import sys, os

def patch(path, old, new):
    s = open(path, encoding='latin1').read()
    if new in s and old not in s:
        print('  already: %s' % os.path.basename(path))
        return
    assert old in s, 'pattern not found in %s: %r' % (path, old[:60])
    open(path, 'w', encoding='latin1').write(s.replace(old, new, 1))
    print('  patched: %s' % path)

def main(root):
    # 1. gettimeofday 原型与 mingw sys/time.h 对齐 (ABI 兼容: timezone* → void*)
    patch(os.path.join(root, 'win32/win32.h'),
          'extern int gettimeofday(struct timeval *, struct timezone *);',
          'extern int gettimeofday(struct timeval *, void *);')
    # 1b. win32.c 里的定义同步改 void*
    patch(os.path.join(root, 'win32/win32.c'),
          'gettimeofday(struct timeval *tv, struct timezone *tz)',
          'gettimeofday(struct timeval *tv, void *tz_v)\n#define tz ((struct timezone *)tz_v)')

    # 2. struct timezone 与 mingw 头重复定义 → 双向守卫(先到者设 _TIMEZONE_DEFINED)
    patch(os.path.join(root, 'win32/win32.h'),
          '''#if defined(__MINGW32__) && !defined(_SYS_TIME_H_) && !defined(_SYS_TIME_H)
struct timezone {
  int tz_minuteswest;
  int tz_dsttime;
};
#endif''',
          '''#if defined(__MINGW32__) && !defined(_TIMEZONE_DEFINED)
#define _TIMEZONE_DEFINED
struct timezone {
  int tz_minuteswest;
  int tz_dsttime;
};
#endif''')

if __name__ == '__main__':
    main(sys.argv[1])
