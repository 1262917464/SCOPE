# RGSS3_300 重建构建脚本（M3 阶段）
#
# 用法（Git Bash / msys2）:
#   make            # 构建 build/RGSS3_300/RGSS300.dll
#   make clean
#
# 依赖：C:\msys64\mingw32 的 i686-w64-mingw32-gcc +
#       build/rubylib/libruby192-static.a（Ruby 1.9.2p0 静态库）

MINGW   ?= /c/msys64/mingw32/bin
CC      := $(MINGW)/i686-w64-mingw32-gcc
WINDRES := $(MINGW)/i686-w64-mingw32-windres

ROOT    := ../..
RUBYLIB := $(ROOT)/build/rubylib
OUTDIR  := $(ROOT)/build/RGSS3_300
TARGET  := $(OUTDIR)/RGSS300.dll

SRCS    := rgss_core.c
OBJS    := $(SRCS:%.c=$(OUTDIR)/%.o)

# -D_WIN32_WINNT 保证 GetUserDefaultUILanguage 等 API 可见
CFLAGS  := -O2 -Wall -std=gnu89 -D_WIN32_WINNT=0x0501 \
           -I. -I$(RUBYLIB)/include -I$(RUBYLIB)/extinclude/i386-mingw32
LDFLAGS := -shared -Wl,--enable-auto-image-base,--enable-auto-import \
           -Wl,--kill-at -static-libgcc
LIBS    := $(RUBYLIB)/libruby192-static.a -lws2_32 -lshell32 -luser32 -lgdi32 -lwinmm -ladvapi32

all: $(TARGET)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/%.o: %.c RGSS300.h | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) RGSS300.def | $(OUTDIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) RGSS300.def $(LIBS)
	@echo "built: $@"

clean:
	rm -rf $(OUTDIR)

.PHONY: all clean
