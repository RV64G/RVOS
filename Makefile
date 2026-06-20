# RISC-V OS Makefile
#
# 主要目标:
#   make        - 构建默认 EFI 应用和内核 ELF
#   make clean  - 清理所有构建文件
#   make run    - 在 QEMU/EDK2 中运行默认 EFI 应用
#   make wall   - 使用严格的警告选项进行构建

include mk/toolchain.mk
include mk/qemu.mk

# --- Build Paths ---
BUILD_DIR  = build
TFTP_ROOT ?= /tmp/rvos-tftp
TFTP_IFACE ?= enp55s0
TFTP_HOST ?= 10.90.50.43
QEMU_TEST_LOG ?= $(BUILD_DIR)/test/qemu-selftest.log
QEMU_TEST_TIMEOUT ?= 90
QEMU_TEST_MARKER ?= Kernel selftest passed

include mk/kernel.mk
include mk/efi.mk

# --- Targets ---
all: efi kernel
	@echo "Build completed successfully"
	@echo "Default artifacts:"
	@echo "  - $(EFI_BOOT_APP) : RISC-V UEFI application"
	@echo "  - $(KERNEL_ELF) : RVOS kernel ELF"

# --- Utility Targets ---
# Clean everything
clean:
	rm -rf $(BUILD_DIR) wall_warnings.log

# Run the default EFI application in QEMU.
$(QEMU_EFI_VARS): $(QEMU_EFI_VARS_TEMPLATE)
	@mkdir -p $(dir $@)
	cp $< $@

run: $(EFI_ESP_IMAGE) $(QEMU_EFI_VARS)
	@$(QEMU) -M ? | grep virt >/dev/null || exit
	@echo "Press Ctrl-A and then X to exit QEMU"
	@echo "------------------------------------"
	@$(call qemu-run,$(EFI_ESP_IMAGE))

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
check-undef: $(KERNEL_ELF)
	@$(NM) -u $(KERNEL_ELF)

check-selftest-undef: $(KERNEL_TEST_ELF)
	@$(NM) -u $(KERNEL_TEST_ELF)

test-build: efi-test-esp check-selftest-undef
	@echo "Selftest build and link checks passed"

test-qemu: $(EFI_TEST_ESP_IMAGE) $(QEMU_EFI_VARS)
	@echo "Running QEMU kernel selftest..."
	@sh scripts/qemu-wait-for-log.sh \
		"$(QEMU_TEST_LOG)" \
		"$(QEMU_TEST_TIMEOUT)" \
		"$(QEMU_TEST_MARKER)" \
		-- \
		$(call qemu-run,$(EFI_TEST_ESP_IMAGE))
	@echo "QEMU log: $(QEMU_TEST_LOG)"

test: test-build test-qemu
	@echo "All tests passed"

# Generate clangd/IDE compile database.
compile-commands:
	@python3 scripts/gen-compile-commands.py \
		--output compile_commands.json \
		--directory "$(CURDIR)" \
		--cc "$(CC)" \
		--kernel-cflags '$(KERNEL_ELF_CFLAGS)' \
		--kernel-sources '$(KERNEL_CLANGD_C_SRCS)' \
		--efi-cflags '$(EFI_CFLAGS)' \
		--efi-sources '$(EFI_APP_SRCS) $(EFI_RISCV_SRCS)'
	@echo "Generated compile_commands.json"

tftp-sync:
	@TFTP_ROOT="$(TFTP_ROOT)" scripts/board-tftp-sync.sh

tftp-serve:
	@TFTP_ROOT="$(TFTP_ROOT)" TFTP_IFACE="$(TFTP_IFACE)" TFTP_HOST="$(TFTP_HOST)" \
		scripts/board-tftp-serve.sh

# Show ELF section sizes
size: $(KERNEL_ELF)
	@$(SIZE) $(KERNEL_ELF)

# Generate kernel disassembly text.
txt: $(KERNEL_ELF)
	@$(OBJDUMP) -S $(KERNEL_ELF) > build/kernel/kernel.txt

# View disassembled code in 'less'
code: $(KERNEL_ELF)
	@$(OBJDUMP) -S $(KERNEL_ELF) | less

# --- Debug and Test Targets ---
# Strictly compile the project, treating warnings as errors
wall:
	@echo "Testing compilation with strict warnings..."
	@echo "This will treat warnings as errors and log output to wall_warnings.log"
	@echo "-----------------------------------------------------------------------"
	@$(MAKE) clean > /dev/null
	@$(MAKE) -k all CFLAGS_WARN="$(CFLAGS_WARN_STRICT)" > wall_warnings.log 2>&1 || { \
		echo "Strict compilation failed. See wall_warnings.log"; \
		exit 1; \
	}
	@echo "Strict compilation finished. Check wall_warnings.log for details."

.PHONY: all clean run wall code txt toolchain check-undef check-selftest-undef test-build test-qemu test compile-commands tftp-sync tftp-serve size
