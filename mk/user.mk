# Standalone user program build rules.

USER_BUILD_DIR := $(BUILD_DIR)/user
USER_ELF       := $(USER_BUILD_DIR)/hello.elf
USER_BLOB_S    := $(USER_BUILD_DIR)/hello_blob.S
USER_BLOB_O    := $(USER_BUILD_DIR)/hello_blob.o

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

$(USER_BLOB_S): $(USER_ELF)
	@mkdir -p $(dir $@)
	@printf '.section .user_elf, "a"\n' > $@
	@printf '.globl __user_elf_start\n' >> $@
	@printf '.globl __user_elf_end\n' >> $@
	@printf '.balign 8\n' >> $@
	@printf '__user_elf_start:\n' >> $@
	@printf '.incbin "$(USER_ELF)"\n' >> $@
	@printf '__user_elf_end:\n' >> $@

$(USER_BLOB_O): $(USER_BLOB_S)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) $(INCLUDES) -MMD -MP -c $< -o $@

user-elf: $(USER_ELF)
	@echo "User ELF generated: $(USER_ELF)"

.PHONY: user-elf
