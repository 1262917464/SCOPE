# RGSS202E (RPG Maker VX) 重建 DLL —— 唯一构建真相（原 PROGRESS.md §六 已降级为说明）
#
# 用法（Git Bash / msys64）:
#   make            # 产出 $(SCOPE_LOCAL)/build/rgss2/RGSS202E_rebuild.dll
#   make clean
#
# 依赖: C:\msys64\mingw32 工具链
#       $(SCOPE_LOCAL)/build/ruby-build        Ruby 1.8.1 静态库 (libmsvcrt-ruby18-static.a)
#       $(SCOPE_LOCAL)/third_party/ruby-1.8.1  zlib 扩展源码
#       环境变量 SCOPE_LOCAL 可覆盖工作台位置，缺省 <repo>/local

REPO := $(abspath ../../..)
SCOPE_LOCAL ?= $(REPO)/local
include ../../mk/common.mk

RB      := $(SCOPE_LOCAL)/build/ruby-build
OUTDIR  := $(SCOPE_LOCAL)/build/rgss2
TARGET  := $(OUTDIR)/RGSS202E_rebuild.dll

SRCS_C   := export.c classes.c bitmap.c data_classes.c graphics_classes.c \
            gamemain.c regex_stubs.c render.c embedded.c marshal.c
SRCS_CXX := image.cpp
OBJS     := $(SRCS_C:%.c=$(OUTDIR)/%.o) $(SRCS_CXX:%.cpp=$(OUTDIR)/%.o) \
            $(OUTDIR)/win32api.o $(OUTDIR)/zlib_ext.o $(OUTDIR)/rpg_src.o

# 调试辅助开关 (debug.h): 1=默认, 日志+崩溃转储可用; 0=1:1 洁净构建, 零日志副作用
RGSS_DEBUG_TOOLS ?= 1
CFLAGS += -DRGSS_DEBUG_TOOLS=$(RGSS_DEBUG_TOOLS)

CFLAGS += -I. -Iext -I$(RB) -I$(RB)/win32

all: $(TARGET)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/%.o: %.c | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/%.o: %.cpp | $(OUTDIR)
	$(CXX) $(CFLAGS) -c $< -o $@

# 重建版 Win32API（修复原版 cdecl/stdcall 双清栈炸弹，见 docs/analysis/common/win32api_cdecl.md）
$(OUTDIR)/win32api.o: ext/Win32API.c | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Ruby zlib 扩展
$(OUTDIR)/zlib_ext.o: $(SCOPE_LOCAL)/third_party/ruby-1.8.1/ext/zlib/zlib.c | $(OUTDIR)
	$(CC) $(CFLAGS) -I$(SCOPE_LOCAL)/third_party/ruby-1.8.1/ext/zlib -c $< -o $@

# 内嵌 RPG 类源码: objcopy 符号名 = 输入路径字符串的变形（_binary_build_rpg_rb_*），
# embedded.c 用 asm 标签精确绑定，故必须以 $(SCOPE_LOCAL) 为 cwd、相对路径 build/rpg.rb 调用
$(OUTDIR)/rpg_src.o: embedded/rpg_classes.rb | $(OUTDIR)
	cp $< $(SCOPE_LOCAL)/build/rpg.rb
	cd $(SCOPE_LOCAL) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 \
	  --rename-section .data=.rdata,alloc,load,readonly,data,contents \
	  build/rpg.rb $(OUTDIR)/rpg_src.o

$(TARGET): $(OBJS) exports.def | $(OUTDIR)
	$(CXX) -shared -O2 -static -Wl,--image-base,0x10000000 -Wl,--disable-dynamicbase \
	  -o $@ $(OBJS) exports.def \
	  -L$(RB) -lmsvcrt-ruby18-static \
	  -lws2_32 -lkernel32 -luser32 -lgdi32 -lwinmm -lmsacm32 -ladvapi32 -lole32 -lz -lgdiplus
	@echo "built: $@"

clean:
	rm -rf $(OUTDIR)

.PHONY: all clean
