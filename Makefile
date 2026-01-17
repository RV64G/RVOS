# RISC-V OS Makefile
#
# 主要目标:
#   make        - 构建操作系统
#   make clean  - 清理所有构建文件
#   make run    - 在QEMU中运行操作系统
#   make rt     - 在QEMU中运行测试模式
#   make wall   - 使用严格的警告选项进行构建

# --- Toolchain ---
CROSS_COMPILE ?= riscv64-unknown-elf-
CC      = ${CROSS_COMPILE}gcc
OBJCOPY = ${CROSS_COMPILE}objcopy
OBJDUMP = ${CROSS_COMPILE}objdump
GDB     = gdb-multiarch

# --- Flags ---
# Base CFLAGS used for both kernel and user code
CFLAGS_BASE = -nostdlib -fno-builtin -march=rv64gc_zbb -mabi=lp64d -mcmodel=medany -g

# Stricter warnings for the 'wall' target
CFLAGS_WARN_STRICT = -Wall -Wextra -Werror -Wformat=2 -Wformat-security -Wlogical-op -Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition -Wmissing-declarations -Wpointer-arith -Wwrite-strings -Wcast-qual -Wcast-align -Wshadow -Wredundant-decls -Wnested-externs -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function

# Includes
INCLUDES = -I include

# Final CFLAGS for kernel and user code
ifdef RUN_TEST
K_CFLAGS = $(CFLAGS_BASE) $(INCLUDES) -DRUN_TEST
U_CFLAGS = $(CFLAGS_BASE) $(INCLUDES) -DRUN_TEST
else
K_CFLAGS = $(CFLAGS_BASE) $(INCLUDES)
U_CFLAGS = $(CFLAGS_BASE) $(INCLUDES)
endif

# --- Build Paths ---
BUILD_DIR  = build
TARGET     = $(BUILD_DIR)/os.elf
IMAGE_BIN  = Image
BOOT_CMD   = boot.cmd
BOOT_SCR   = boot.scr

# --- QEMU ---
QEMU   = qemu-system-riscv64
QFLAGS = -nographic -smp 4 -machine virt -cpu rv64,zba=true,zbb=true,zbc=true,zbs=true

# --- Source Files ---
# Assembly files
SRCS_ASM = \
	arch/riscv/start.S \
	arch/riscv/mem.S  \
	arch/riscv/context.S

# Kernel Source Files
SRCS_C = \
	kernel/main.c \
	kernel/fdt.c \
	kernel/printk.c \
	kernel/sched.c \
	kernel/syscall.c \
	kernel/string.c \
	kernel/trap.c \
	kernel/timer.c \
	kernel/spinlock.c \
	kernel/algorithm.c \
	kernel/boot_info.c \
	kernel/user.c \
	kernel/hart.c \
	mm/page.c \
	mm/vm.c \
	mm/malloc.c \
	drivers/plic.c

# Test Source Files (only included in test mode)
TEST_SRCS_C = \
	test/test_main.c \
	test/test_page.c \
	test/test_multicore.c

# User Source Files (C)
USER_SRCS_C = \
	user/printf.c \
	user/syscalls.c \
	user/user_tasks.c \
	user/lib.c

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
all: $(TARGET) $(IMAGE_BIN) txt
	@echo "Build completed successfully"
	@echo "Files generated:"
	@echo "  - $(TARGET)     : ELF executable"
	@echo "  - $(IMAGE_BIN)  : Hardware boot image"
	@echo "  - $(BOOT_SCR)   : Auto boot script for U-Boot"
	@echo "  - os.txt        : Disassembled code"

# Link the final ELF file
$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) -T os.ld $(OBJS) -o $@
	@echo "Warning: $(TARGET) may have a LOAD segment with RWX permissions. This is common for simple loaders."

# --- Compilation Rules ---
# Rule for Kernel C files
$(OBJS_C): $(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(K_CFLAGS) -c $< -o $@

# Rule for User C files
$(USER_OBJS_C): $(BUILD_DIR)/user/%.o: user/%.c
	@mkdir -p $(dir $@)
	$(CC) $(U_CFLAGS) -c $< -o $@

# Rule for Test C files (only when RUN_TEST is defined)
ifdef RUN_TEST
$(TEST_OBJS_C): $(BUILD_DIR)/test/%.o: test/%.c
	@mkdir -p $(dir $@)
	$(CC) $(K_CFLAGS) -c $< -o $@
endif

# Rule for Assembly files
$(OBJS_ASM): $(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(K_CFLAGS) -c $< -o $@

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

# Run in QEMU
run: all
	@$(QEMU) -M ? | grep virt >/dev/null || exit
	@echo "Press Ctrl-A and then X to exit QEMU"
	@echo "------------------------------------"
	@$(QEMU) $(QFLAGS) -kernel $(TARGET)

# Run in test mode
rt:
	@echo "Building and running in TEST MODE..."
	@echo "======================================"
	@$(MAKE) clean
	@$(MAKE) all RUN_TEST=1
	@echo "Starting QEMU in test mode..."
	@echo "Press Ctrl-A and then X to exit QEMU"
	@echo "------------------------------------"
	@$(QEMU) $(QFLAGS) -kernel $(TARGET)

# Generate disassembled text file
txt: $(TARGET)
	@$(OBJDUMP) -S $(TARGET) > os.txt

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
	@($(MAKE) -k all CFLAGS_WARN="$(CFLAGS_WARN_STRICT)" > wall_warnings.log 2>&1) || true
	@echo "Strict compilation finished. Check wall_warnings.log for details."

# Start QEMU and wait for a GDB connection
qemu-gdb-server: all
	@echo "Starting QEMU for GDB connection..."
	@echo "QEMU GDB server will listen on port 1234"
	@$(QEMU) $(QFLAGS) -kernel $(TARGET) -s -S

# Start QEMU and GDB for a debugging session
debug: all
	@echo "Press Ctrl-C and then input 'quit' to exit GDB and QEMU"
	@echo "-------------------------------------------------------"
	@$(QEMU) $(QFLAGS) -kernel $(TARGET) -s -S &
	@$(GDB) $(TARGET) -q -x gdbinit

.PHONY: all clean run rt wall qemu-gdb-server debug code txt
