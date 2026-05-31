# QEMU settings.

QEMU ?= qemu-system-riscv64
QFLAGS ?= -nographic -smp 4 -machine virt -cpu rv64,zba=true,zbb=true,zbc=true,zbs=true
