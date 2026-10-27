# SCOPE 工具构建入口 —— 全部自研工具的可复现构建（ADR-0001 刀 3）
#
# 用法: make -C tools [all|clean|<name>]
# 产物: $(SCOPE_LOCAL)/build/tools/<name>.exe   （源码与产物分离，源码树永不写入）
# 说明: 个别工具依赖运行期环境（游戏副本、原版 DLL），编译不过=记录跳过，不阻塞其他目标

REPO := $(abspath ..)
SCOPE_LOCAL ?= $(REPO)/local
include ../src/mk/common.mk   # 路径按 tools/ 相对位置：../src/mk/common.mk

OUT := $(SCOPE_LOCAL)/build/tools
TOOLDIR := $(SCOPE_LOCAL)/build/tools-unpack

CFLAGS += -I$(MINGW)/../include
LDLIBS ?= -luser32 -lgdi32 -lwinmm

# ---- 探针（probe/）----
PROBES := iat_probe keytest blinkprobe cursortest probe_input_consts probe_input2 \
          rgss_introspect rgss_introspect2 smoke difftest
# 跳过记录（G5）：RGSS1_probelog.c 为 WinMain 入口（原仅编 .o 未出 exe），不在本表内

# ---- 验证（verify/）----
VERIFIES := verify_bitmap verify_rebuild

# ---- 脱壳（unpack/）----
UNPACKERS := unpacker unpacker2 unpacker3 RGSS1_unpacker3b
# 跳过记录（G5）：RGSS1_fixtick.c 无 main（作为 fixstub.dll 的 get_tick 辅助对象链接）

ALL_EXES := $(PROBES:%=$(OUT)/%.exe) $(VERIFIES:%=$(OUT)/%.exe) $(UNPACKERS:%=$(OUT)/%.exe)
ALL_DLLS := $(OUT)/rgssprobe.dll $(OUT)/RGSS1_fixstub.dll

all: $(ALL_EXES) $(ALL_DLLS)
	@echo "tools build done -> $(OUT)"

$(OUT):
	mkdir -p $(OUT)

define COMPILE
$$(OUT)/$(1).exe: $(2) | $$(OUT)
	$$(CC) $$(CFLAGS) -o $$@ $$< $$(LDLIBS) $(3)
endef

# probe 桶（smoke/difftest 无额外库）
$(foreach t,$(PROBES),$(eval $(call COMPILE,$(t),probe/$(t).c,)))
# verify 桶
$(foreach t,$(VERIFIES),$(eval $(call COMPILE,$(t),verify/$(t).c,)))
# unpack 桶
$(foreach t,$(UNPACKERS),$(eval $(call COMPILE,$(t),unpack/$(t).c,)))

# 探针 DLL（M1.5 可运行脱壳版依赖）：probe.S 的 log_call 由 probelog.c 提供
$(OUT)/rgssprobe.dll: analysis/probe.S analysis/probelog.c analysis/probe.def | $(OUT)
	$(CC) -shared -o $@ $^

# RGSS1 修复桩：fixstub.S 的 get_tick 由 RGSS1_fixtick.c 提供
$(OUT)/RGSS1_fixstub.dll: unpack/RGSS1_fixstub.S unpack/RGSS1_fixtick.c unpack/RGSS1_fixstub.def | $(OUT)
	$(CC) -shared -o $@ $^

clean:
	rm -rf $(OUT)

.PHONY: all clean
