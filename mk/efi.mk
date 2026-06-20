# EFI application build rules.

EFI_BUILD_DIR := $(BUILD_DIR)/efi
EFI_APP_NAME  := boot
EFI_SKIP_EXIT_BOOT_SERVICES ?= 0
EFI_APP_SRCS  := $(wildcard boot/efi/$(EFI_APP_NAME)/*.c)
EFI_APP_ASMS  := $(wildcard boot/efi/$(EFI_APP_NAME)/*.S)
EFI_RISCV_SRCS := $(wildcard boot/efi/riscv/*.c)
EFI_RISCV_ASMS := $(wildcard boot/efi/riscv/*.S)
EFI_APP_OBJS  := \
	$(patsubst boot/efi/$(EFI_APP_NAME)/%.c,$(EFI_BUILD_DIR)/%.o,$(EFI_APP_SRCS)) \
	$(patsubst boot/efi/$(EFI_APP_NAME)/%.S,$(EFI_BUILD_DIR)/%.o,$(EFI_APP_ASMS)) \
	$(patsubst boot/efi/riscv/%.c,$(EFI_BUILD_DIR)/riscv/%.o,$(EFI_RISCV_SRCS)) \
	$(patsubst boot/efi/riscv/%.S,$(EFI_BUILD_DIR)/riscv/%.o,$(EFI_RISCV_ASMS))
EFI_APP_ELF   := $(EFI_BUILD_DIR)/$(EFI_APP_NAME).so
EFI_BOOT_APP  := $(EFI_BUILD_DIR)/BOOTRISCV64.EFI
EFI_ESP_IMAGE := $(EFI_BUILD_DIR)/esp.img
EFI_TEST_ESP_IMAGE := $(BUILD_DIR)/test/esp.img

EFI_OBJCOPY ?= $(shell if command -v riscv64-elf-objcopy >/dev/null 2>&1; then printf 'riscv64-elf-objcopy'; elif command -v riscv64-unknown-elf-objcopy >/dev/null 2>&1; then printf 'riscv64-unknown-elf-objcopy'; else printf 'riscv64-elf-objcopy'; fi)

EFI_CFLAGS = \
	--target=riscv64-unknown-elf \
	-ffreestanding \
	-fshort-wchar \
	-fno-stack-protector \
	-fno-builtin \
	-fPIC \
	-Iboot/efi/include \
	-Iinclude \
	-DEFI_SKIP_EXIT_BOOT_SERVICES=$(EFI_SKIP_EXIT_BOOT_SERVICES) \
	-mno-relax \
	-g

EFI_LDFLAGS = \
	--target=riscv64-unknown-elf \
	-fuse-ld=lld \
	-nostdlib \
	-Wl,-shared \
	-Wl,-Bsymbolic \
	-Wl,-e,efi_main \
	-Wl,-T,boot/efi/efi.lds \
	-Wl,--no-relax

$(EFI_BUILD_DIR)/%.o: boot/efi/$(EFI_APP_NAME)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(EFI_CFLAGS) -c $< -o $@

$(EFI_BUILD_DIR)/%.o: boot/efi/$(EFI_APP_NAME)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(EFI_CFLAGS) -c $< -o $@

$(EFI_BUILD_DIR)/riscv/%.o: boot/efi/riscv/%.c
	@mkdir -p $(dir $@)
	$(CC) $(EFI_CFLAGS) -c $< -o $@

$(EFI_BUILD_DIR)/riscv/%.o: boot/efi/riscv/%.S
	@mkdir -p $(dir $@)
	$(CC) $(EFI_CFLAGS) -c $< -o $@

$(EFI_APP_ELF): $(EFI_APP_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(EFI_LDFLAGS) $^ -o $@

$(EFI_BOOT_APP): $(EFI_APP_ELF)
	@mkdir -p $(dir $@)
	$(EFI_OBJCOPY) \
		--strip-all \
		-j .text -j .rodata -j '.rodata.*' -j .data -j .sdata -j .reloc \
		-O efi-app-riscv64 $< $@

$(EFI_ESP_IMAGE): $(EFI_BOOT_APP) $(KERNEL_ELF)
	@mkdir -p $(dir $@)
	rm -f $@
	truncate -s 64M $@
	mkfs.vfat -F 32 -n RVOS $@
	mmd -i $@ ::/EFI
	mmd -i $@ ::/EFI/BOOT
	mmd -i $@ ::/RVOS
	mcopy -i $@ $(EFI_BOOT_APP) ::/EFI/BOOT/BOOTRISCV64.EFI
	mcopy -i $@ $(KERNEL_ELF) ::/RVOS/KERNEL.ELF

$(EFI_TEST_ESP_IMAGE): $(EFI_BOOT_APP) $(KERNEL_TEST_ELF)
	@mkdir -p $(dir $@)
	rm -f $@
	truncate -s 64M $@
	mkfs.vfat -F 32 -n RVOS-TEST $@
	mmd -i $@ ::/EFI
	mmd -i $@ ::/EFI/BOOT
	mmd -i $@ ::/RVOS
	mcopy -i $@ $(EFI_BOOT_APP) ::/EFI/BOOT/BOOTRISCV64.EFI
	mcopy -i $@ $(KERNEL_TEST_ELF) ::/RVOS/KERNEL.ELF

efi: $(EFI_BOOT_APP)
	@echo "EFI application generated: $(EFI_BOOT_APP)"

efi-esp: $(EFI_ESP_IMAGE)
	@echo "EFI system partition image generated: $(EFI_ESP_IMAGE)"

efi-test-esp: $(EFI_TEST_ESP_IMAGE)
	@echo "EFI selftest partition image generated: $(EFI_TEST_ESP_IMAGE)"

efi-info: $(EFI_BOOT_APP)
	@file $(EFI_BOOT_APP)

.PHONY: efi efi-esp efi-test-esp efi-info
