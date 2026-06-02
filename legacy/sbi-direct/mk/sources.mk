# 源文件扫描规则。

ASM_SOURCE_DIRS := arch/riscv
KERNEL_SOURCE_DIRS := kernel mm drivers
USER_SOURCE_DIRS := user
TEST_SOURCE_DIRS := test

ENTRY_ASM := arch/riscv/start.S
DISCOVERED_ASM := $(sort $(foreach dir,$(ASM_SOURCE_DIRS),$(wildcard $(dir)/*.S)))
SRCS_ASM := $(ENTRY_ASM) $(filter-out $(ENTRY_ASM),$(DISCOVERED_ASM))
SRCS_C := $(sort $(foreach dir,$(KERNEL_SOURCE_DIRS),$(wildcard $(dir)/*.c)))
USER_SRCS_C := $(sort $(foreach dir,$(USER_SOURCE_DIRS),$(wildcard $(dir)/*.c)))
TEST_SRCS_C := $(sort $(foreach dir,$(TEST_SOURCE_DIRS),$(wildcard $(dir)/*.c)))
