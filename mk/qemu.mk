# QEMU settings.

QEMU ?= qemu-system-riscv64

QEMU_EFI_CODE ?= /usr/share/edk2/riscv64/RISCV_VIRT_CODE.fd
QEMU_EFI_VARS_TEMPLATE ?= /usr/share/edk2/riscv64/RISCV_VIRT_VARS.fd
QEMU_EFI_VARS ?= $(BUILD_DIR)/efi/RISCV_VIRT_VARS.fd
QEMU_MEMORY ?= 512M

QEMU_BASE_QFLAGS = \
	-nographic \
	-machine virt,acpi=off,pflash0=pflash0,pflash1=pflash1 \
	-cpu rv64,zba=true,zbb=true,zbc=true,zbs=true \
	-m $(QEMU_MEMORY) \
	-smp 4 \
	-blockdev node-name=pflash0,driver=file,read-only=on,filename=$(QEMU_EFI_CODE) \
	-blockdev node-name=pflash1,driver=file,filename=$(QEMU_EFI_VARS)

QEMU_USB_STORAGE_QFLAGS = \
	-device qemu-xhci \
	-device usb-storage,drive=esp

define qemu-run
$(QEMU) $(QEMU_BASE_QFLAGS) -drive file=$(1),format=raw,if=none,id=esp $(QEMU_USB_STORAGE_QFLAGS)
endef
