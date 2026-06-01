# RISC-V OS Makefile
#
# 主要目标:
#   make        - 构建默认 EFI 应用和内核 ELF
#   make clean  - 清理所有构建文件
#   make image  - 构建旧版裸内核镜像
#   make run    - 在 QEMU/EDK2 中运行默认 EFI 应用
#   make rt     - 在QEMU中运行测试模式
#   make wall   - 使用严格的警告选项进行构建

include mk/toolchain.mk
include mk/sources.mk
include mk/qemu.mk

RVOS_CFLAGS = $(CFLAGS_BASE) $(INCLUDES) $(CFLAGS_WARN)
ifdef RUN_TEST
RVOS_CFLAGS += -DRUN_TEST
endif

# --- Build Paths ---
BUILD_DIR  = build
TARGET     = $(BUILD_DIR)/os.elf
IMAGE_BIN  = Image
BOOT_CMD   = boot.cmd
BOOT_SCR   = boot.scr

include mk/kernel.mk
include mk/efi.mk

# --- Object Files ---
OBJS_ASM = $(addprefix $(BUILD_DIR)/, $(addsuffix .o, $(basename $(SRCS_ASM))))
OBJS_C   = $(addprefix $(BUILD_DIR)/, $(addsuffix .o, $(basename $(SRCS_C))))
USER_OBJS_C = $(addprefix $(BUILD_DIR)/, $(addsuffix .o, $(basename $(USER_SRCS_C))))

# Test object files (conditionally included)
ifdef RUN_TEST
TEST_OBJS_C = $(addprefix $(BUILD_DIR)/, $(addsuffix .o, $(basename $(TEST_SRCS_C))))
OBJS     = $(OBJS_ASM) $(OBJS_C) $(USER_OBJS_C) $(TEST_OBJS_C)
else
OBJS     = $(OBJS_ASM) $(OBJS_C) $(USER_OBJS_C)
endif

# --- Targets ---
all: efi kernel
	@echo "Build completed successfully"
	@echo "Default artifacts:"
	@echo "  - $(EFI_BOOT_APP) : RISC-V UEFI application"
	@echo "  - $(KERNEL_ELF) : RVOS kernel ELF"

image: $(TARGET) $(IMAGE_BIN) txt
	@echo "Build completed successfully"
	@echo "Files generated:"
	@echo "  - $(TARGET)     : ELF executable"
	@echo "  - $(IMAGE_BIN)  : Hardware boot image"
	@echo "  - $(BOOT_SCR)   : Auto boot script for U-Boot"
	@echo "  - os.txt        : Disassembled code"

# Link the final ELF file
$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS_BASE) -T os.ld $(OBJS) -o $@
	@echo "Warning: $(TARGET) may have a LOAD segment with RWX permissions. This is common for simple loaders."

# --- Compilation Rules ---
# Rule for Kernel C files
$(OBJS_C): $(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(RVOS_CFLAGS) -c $< -o $@

# Rule for User C files
$(USER_OBJS_C): $(BUILD_DIR)/user/%.o: user/%.c
	@mkdir -p $(dir $@)
	$(CC) $(RVOS_CFLAGS) -c $< -o $@

# Rule for Test C files (only when RUN_TEST is defined)
ifdef RUN_TEST
$(TEST_OBJS_C): $(BUILD_DIR)/test/%.o: test/%.c
	@mkdir -p $(dir $@)
	$(CC) $(RVOS_CFLAGS) -c $< -o $@
endif

# Rule for Assembly files
$(OBJS_ASM): $(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(RVOS_CFLAGS) -c $< -o $@

# --- Hardware Image Generation ---
# Generate Image and boot.scr (for real hardware)
$(IMAGE_BIN): $(TARGET)
	@echo "Generating Image and boot.scr..."
	$(OBJCOPY) -O binary --set-start 0x80200000 $< $(IMAGE_BIN)
	@echo "fatload mmc 1:1 0x80200000 $(IMAGE_BIN)" > $(BOOT_CMD)
	@echo "go 0x80200000" >> $(BOOT_CMD)
	mkimage -C none -A riscv -T script -d $(BOOT_CMD) $(BOOT_SCR)
	@echo "Image and boot.scr generated successfully"
	@echo "Copy both to SD card (FAT32) for auto boot"

# --- Utility Targets ---
# Clean everything
clean:
	rm -rf $(BUILD_DIR) os.txt $(IMAGE_BIN) $(BOOT_CMD) $(BOOT_SCR) wall_warnings.log

# Run the default EFI application in QEMU.
$(QEMU_EFI_VARS): $(QEMU_EFI_VARS_TEMPLATE)
	@mkdir -p $(dir $@)
	cp $< $@

run: $(EFI_ESP_IMAGE) $(QEMU_EFI_VARS)
	@$(QEMU) -M ? | grep virt >/dev/null || exit
	@echo "Press Ctrl-A and then X to exit QEMU"
	@echo "------------------------------------"
	@$(QEMU) $(QEMU_EFI_QFLAGS)

# Run the legacy raw kernel image in QEMU.
run-image: image
	@$(QEMU) -M ? | grep virt >/dev/null || exit
	@echo "Press Ctrl-A and then X to exit QEMU"
	@echo "------------------------------------"
	@$(QEMU) $(QFLAGS) -kernel $(TARGET)

# Run in test mode
rt:
	@echo "Building and running in TEST MODE..."
	@echo "======================================"
	@$(MAKE) clean
	@$(MAKE) image RUN_TEST=1
	@echo "Starting QEMU in test mode..."
	@echo "Press Ctrl-A and then X to exit QEMU"
	@echo "------------------------------------"
	@$(QEMU) $(QFLAGS) -kernel $(TARGET)

# Generate disassembled text file
txt: $(TARGET)
	@$(OBJDUMP) -S $(TARGET) > os.txt

# Print selected toolchain commands
toolchain:
	@echo "TOOLCHAIN=$(TOOLCHAIN)"
	@echo "CC=$(CC)"
	@echo "OBJCOPY=$(OBJCOPY)"
	@echo "OBJDUMP=$(OBJDUMP)"
	@echo "NM=$(NM)"
	@echo "READELF=$(READELF)"
	@echo "SIZE=$(SIZE)"
	@echo "GDB=$(GDB)"
	@echo "RISCV_MARCH=$(RISCV_MARCH)"
	@echo "RISCV_ABI=$(RISCV_ABI)"

# Show undefined symbols in the linked ELF
check-undef: $(TARGET)
	@$(NM) -u $(TARGET)

# Show ELF section sizes
size: $(TARGET)
	@$(SIZE) $(TARGET)

# View disassembled code in 'less'
code: $(TARGET)
	@$(OBJDUMP) -S $(TARGET) | less

# --- Debug and Test Targets ---
# Strictly compile the project, treating warnings as errors
wall:
	@echo "Testing compilation with strict warnings..."
	@echo "This will treat warnings as errors and log output to wall_warnings.log"
	@echo "-----------------------------------------------------------------------"
	@$(MAKE) clean > /dev/null
	@($(MAKE) -k image CFLAGS_WARN="$(CFLAGS_WARN_STRICT)" > wall_warnings.log 2>&1) || true
	@echo "Strict compilation finished. Check wall_warnings.log for details."

# Start QEMU and wait for a GDB connection
qemu-gdb-server: image
	@echo "Starting QEMU for GDB connection..."
	@echo "QEMU GDB server will listen on port 1234"
	@$(QEMU) $(QFLAGS) -kernel $(TARGET) -s -S

# Start QEMU and GDB for a debugging session
debug: image
	@echo "Press Ctrl-C and then input 'quit' to exit GDB and QEMU"
	@echo "-------------------------------------------------------"
	@$(QEMU) $(QFLAGS) -kernel $(TARGET) -s -S &
	@$(GDB) $(TARGET) -q -x gdbinit

	.PHONY: all image clean run run-image rt wall qemu-gdb-server debug code txt toolchain check-undef size
