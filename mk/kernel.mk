# RVOS kernel ELF 构建规则。

KERNEL_BUILD_DIR := $(BUILD_DIR)/kernel
KERNEL_ELF       := $(KERNEL_BUILD_DIR)/kernel.elf
KERNEL_LINKER    := kernel/kernel.lds
KERNEL_SOURCE_DIRS := kernel mm arch/riscv
KERNEL_SELFTEST_SRC := kernel/selftest.c
KERNEL_ELF_C_SRCS  := $(filter-out $(KERNEL_SELFTEST_SRC),$(sort $(foreach dir,$(KERNEL_SOURCE_DIRS),$(wildcard $(dir)/*.c))))
KERNEL_ELF_S_SRCS  := $(sort $(foreach dir,$(KERNEL_SOURCE_DIRS),$(wildcard $(dir)/*.S)))
KERNEL_ELF_SRCS    := $(KERNEL_ELF_C_SRCS) $(KERNEL_ELF_S_SRCS)
KERNEL_ELF_OBJS    := $(addprefix $(KERNEL_BUILD_DIR)/,$(addsuffix .o,$(basename $(KERNEL_ELF_SRCS))))
KERNEL_ELF_DEPS    := $(KERNEL_ELF_OBJS:.o=.d)

KERNEL_ELF_CFLAGS = $(CFLAGS_BASE) $(INCLUDES) -Ikernel -Imm -Iarch/riscv $(CFLAGS_WARN)
KERNEL_DEPFLAGS   = -MMD -MP

KERNEL_TEST_BUILD_DIR := $(BUILD_DIR)/test/kernel
KERNEL_TEST_ELF       := $(KERNEL_TEST_BUILD_DIR)/kernel.elf
KERNEL_TEST_C_SRCS    := $(KERNEL_ELF_C_SRCS) $(KERNEL_SELFTEST_SRC)
KERNEL_TEST_S_SRCS    := $(KERNEL_ELF_S_SRCS)
KERNEL_TEST_SRCS      := $(KERNEL_TEST_C_SRCS) $(KERNEL_TEST_S_SRCS)
KERNEL_TEST_OBJS      := $(addprefix $(KERNEL_TEST_BUILD_DIR)/,$(addsuffix .o,$(basename $(KERNEL_TEST_SRCS))))
KERNEL_TEST_DEPS      := $(KERNEL_TEST_OBJS:.o=.d)
KERNEL_TEST_CFLAGS    := $(KERNEL_ELF_CFLAGS) -DKERNEL_SELFTEST

$(KERNEL_BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_ELF_CFLAGS) $(KERNEL_DEPFLAGS) -c $< -o $@

$(KERNEL_BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_ELF_CFLAGS) $(KERNEL_DEPFLAGS) -c $< -o $@

$(KERNEL_ELF): $(KERNEL_ELF_OBJS) $(KERNEL_LINKER)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS_BASE) -T $(KERNEL_LINKER) $(KERNEL_ELF_OBJS) -o $@

$(KERNEL_TEST_BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_TEST_CFLAGS) $(KERNEL_DEPFLAGS) -c $< -o $@

$(KERNEL_TEST_BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_TEST_CFLAGS) $(KERNEL_DEPFLAGS) -c $< -o $@

$(KERNEL_TEST_ELF): $(KERNEL_TEST_OBJS) $(KERNEL_LINKER)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS_BASE) -T $(KERNEL_LINKER) $(KERNEL_TEST_OBJS) -o $@

kernel: $(KERNEL_ELF)
	@echo "Kernel ELF generated: $(KERNEL_ELF)"

kernel-selftest: $(KERNEL_TEST_ELF)
	@echo "Kernel selftest ELF generated: $(KERNEL_TEST_ELF)"

.PHONY: kernel kernel-selftest

-include $(KERNEL_ELF_DEPS) $(KERNEL_TEST_DEPS)
