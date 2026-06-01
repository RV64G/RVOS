# QEMU settings.

QEMU ?= qemu-system-riscv64
QFLAGS ?= -nographic -smp 4 -machine virt -cpu rv64,zba=true,zbb=true,zbc=true,zbs=true

QEMU_EFI_CODE ?= /usr/share/edk2/riscv64/RISCV_VIRT_CODE.fd
QEMU_EFI_VARS_TEMPLATE ?= /usr/share/edk2/riscv64/RISCV_VIRT_VARS.fd
QEMU_EFI_VARS ?= $(BUILD_DIR)/efi/RISCV_VIRT_VARS.fd
QEMU_EFI_QFLAGS ?= \
	-nographic \
	-machine virt,acpi=off,pflash0=pflash0,pflash1=pflash1 \
	-cpu rv64,zba=true,zbb=true,zbc=true,zbs=true \
	-m 2G \
	-smp 4 \
	-blockdev node-name=pflash0,driver=file,read-only=on,filename=$(QEMU_EFI_CODE) \
	-blockdev node-name=pflash1,driver=file,filename=$(QEMU_EFI_VARS) \
	-drive file=$(EFI_ESP_IMAGE),format=raw,if=none,id=esp \
	-device qemu-xhci \
	-device usb-storage,drive=esp
