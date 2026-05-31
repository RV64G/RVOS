# EFI application build rules.

EFI_BUILD_DIR := $(BUILD_DIR)/efi
EFI_APP_NAME  := boot
EFI_APP_ELF   := $(EFI_BUILD_DIR)/$(EFI_APP_NAME).so
EFI_APP_OBJ   := $(EFI_BUILD_DIR)/$(EFI_APP_NAME).o
EFI_BOOT_APP  := $(EFI_BUILD_DIR)/BOOTRISCV64.EFI

EFI_OBJCOPY ?= $(shell if command -v riscv64-elf-objcopy >/dev/null 2>&1; then printf 'riscv64-elf-objcopy'; elif command -v riscv64-unknown-elf-objcopy >/dev/null 2>&1; then printf 'riscv64-unknown-elf-objcopy'; else printf 'riscv64-elf-objcopy'; fi)

EFI_CFLAGS = \
	--target=riscv64-unknown-elf \
	-ffreestanding \
	-fshort-wchar \
	-fno-stack-protector \
	-fno-builtin \
	-fPIC \
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

$(EFI_APP_OBJ): boot/efi/$(EFI_APP_NAME)/main.c
	@mkdir -p $(dir $@)
	$(CC) $(EFI_CFLAGS) -c $< -o $@

$(EFI_APP_ELF): $(EFI_APP_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(EFI_LDFLAGS) $< -o $@

$(EFI_BOOT_APP): $(EFI_APP_ELF)
	@mkdir -p $(dir $@)
	$(EFI_OBJCOPY) \
		--strip-all \
		-j .text -j .rodata -j '.rodata.*' -j .data -j .sdata -j .reloc \
		-O efi-app-riscv64 $< $@

efi: $(EFI_BOOT_APP)
	@echo "EFI application generated: $(EFI_BOOT_APP)"

efi-info: $(EFI_BOOT_APP)
	@file $(EFI_BOOT_APP)

.PHONY: efi efi-info
