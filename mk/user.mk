# Standalone user program build rules.

USER_BUILD_DIR := $(BUILD_DIR)/user
USER_ELF       := $(USER_BUILD_DIR)/hello.elf
USER_INITRAMFS := $(USER_BUILD_DIR)/initramfs.img
USER_BLOB_S    := $(USER_BUILD_DIR)/initramfs_blob.S
USER_BLOB_O    := $(USER_BUILD_DIR)/initramfs_blob.o

USER_CFLAGS = \
	$(CC_TARGET_FLAGS) \
	-nostdlib \
	-ffreestanding \
	-fno-builtin \
	-fno-stack-protector \
	-march=$(RISCV_MARCH) \
	-mabi=$(RISCV_ABI) \
	-mcmodel=medany \
	-I include \
	-g

USER_LDFLAGS = \
	$(LD_TARGET_FLAGS) \
	-nostdlib \
	-march=$(RISCV_MARCH) \
	-mabi=$(RISCV_ABI) \
	-mcmodel=medany \
	-Wl,-T,user/user.lds \
	-Wl,--no-relax \
	-g

$(USER_BUILD_DIR)/hello.o: user/hello.S include/syscall_numbers.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_ELF): $(USER_BUILD_DIR)/hello.o user/user.lds
	@mkdir -p $(dir $@)
	$(CC) $(USER_LDFLAGS) $(USER_BUILD_DIR)/hello.o -o $@

$(USER_INITRAMFS): $(USER_ELF) scripts/build-initramfs.py
	@mkdir -p $(dir $@)
	python3 scripts/build-initramfs.py \
		--output $@ \
		--file /bin/hello=$(USER_ELF)

$(USER_BLOB_S): $(USER_INITRAMFS) mk/user.mk
	@mkdir -p $(dir $@)
	@{ \
		printf '.section .initramfs, "a"\n'; \
		printf '.globl __initramfs_start\n'; \
		printf '.globl __initramfs_end\n'; \
		printf '.balign 8\n'; \
		printf '__initramfs_start:\n'; \
		printf '.incbin "$(USER_INITRAMFS)"\n'; \
		printf '__initramfs_end:\n'; \
	} > $@.tmp
	mv $@.tmp $@

$(USER_BLOB_O): $(USER_BLOB_S)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) $(INCLUDES) -MMD -MP -c $< -o $@

user-elf: $(USER_ELF) $(USER_INITRAMFS)
	@echo "User ELF generated: $(USER_ELF)"
	@echo "Initramfs generated: $(USER_INITRAMFS)"

.PHONY: user-elf
