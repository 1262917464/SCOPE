# SCOPE 公共构建设置 —— 只放工具链变量与模式规则，不放任何具体目标（ADR-0001）
# 各运行时 Makefile: include <相对路径>/mk/common.mk
# 工作台位置：环境变量 SCOPE_LOCAL 优先，缺省 <repo>/local（README「仓库边界」）

MINGW   ?= /c/msys64/mingw32/bin
CC      := $(MINGW)/i686-w64-mingw32-gcc
CXX     := $(MINGW)/i686-w64-mingw32-g++
WINDRES := $(MINGW)/i686-w64-mingw32-windres
OBJCOPY := $(MINGW)/objcopy

CFLAGS_COMMON := -O2 -Wall -std=gnu89
LDFLAGS_DLL   := -shared -static-libgcc
