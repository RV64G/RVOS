# RVOS kernel ELF 构建规则。

KERNEL_BUILD_DIR := $(BUILD_DIR)/kernel
KERNEL_ELF       := $(KERNEL_BUILD_DIR)/kernel.elf
KERNEL_LINKER    := kernel/kernel.lds
KERNEL_SOURCE_DIRS := kernel mm
KERNEL_ELF_SRCS  := $(sort $(foreach dir,$(KERNEL_SOURCE_DIRS),$(wildcard $(dir)/*.c)))
KERNEL_ELF_OBJS  := $(addprefix $(KERNEL_BUILD_DIR)/,$(addsuffix .o,$(basename $(KERNEL_ELF_SRCS))))

KERNEL_ELF_CFLAGS = $(CFLAGS_BASE) $(INCLUDES) -Ikernel -Imm $(CFLAGS_WARN)

$(KERNEL_BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_ELF_CFLAGS) -c $< -o $@

$(KERNEL_ELF): $(KERNEL_ELF_OBJS) $(KERNEL_LINKER)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS_BASE) -T $(KERNEL_LINKER) $(KERNEL_ELF_OBJS) -o $@

kernel: $(KERNEL_ELF)
	@echo "Kernel ELF generated: $(KERNEL_ELF)"

.PHONY: kernel
