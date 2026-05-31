# Toolchain selection.
#
# Usage:
#   make TOOLCHAIN=gcc
#   make TOOLCHAIN=clang

TOOLCHAIN ?= clang

RISCV_MARCH ?= rv64gc_zbb
RISCV_ABI   ?= lp64d

ifeq ($(TOOLCHAIN),clang)
CC       := clang
OBJCOPY  := llvm-objcopy
OBJDUMP  := llvm-objdump
NM       := llvm-nm
READELF  := llvm-readelf
SIZE     := llvm-size
GDB      ?= gdb

CC_TARGET_FLAGS := --target=riscv64-unknown-elf
LD_TARGET_FLAGS := --target=riscv64-unknown-elf -fuse-ld=lld
else ifeq ($(TOOLCHAIN),gcc)
ifndef CROSS_COMPILE
CROSS_COMPILE := $(shell if command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then printf 'riscv64-unknown-elf-'; elif command -v riscv64-elf-gcc >/dev/null 2>&1; then printf 'riscv64-elf-'; else printf 'riscv64-unknown-elf-'; fi)
endif

CC       := $(CROSS_COMPILE)gcc
OBJCOPY  := $(CROSS_COMPILE)objcopy
OBJDUMP  := $(CROSS_COMPILE)objdump
NM       := $(CROSS_COMPILE)nm
READELF  := $(CROSS_COMPILE)readelf
SIZE     := $(CROSS_COMPILE)size
GDB      ?= riscv64-elf-gdb

CC_TARGET_FLAGS :=
LD_TARGET_FLAGS :=
else
$(error Unsupported TOOLCHAIN '$(TOOLCHAIN)'; use TOOLCHAIN=gcc or TOOLCHAIN=clang)
endif

CFLAGS_BASE = \
	$(CC_TARGET_FLAGS) \
	-nostdlib \
	-ffreestanding \
	-fno-builtin \
	-fno-stack-protector \
	-march=$(RISCV_MARCH) \
	-mabi=$(RISCV_ABI) \
	-mcmodel=medany \
	-g

LDFLAGS_BASE = \
	$(LD_TARGET_FLAGS) \
	-nostdlib \
	-march=$(RISCV_MARCH) \
	-mabi=$(RISCV_ABI) \
	-mcmodel=medany \
	-g

CFLAGS_WARN_STRICT = \
	-Wall \
	-Wextra \
	-Werror \
	-Wformat=2 \
	-Wformat-security \
	-Wlogical-op \
	-Wmissing-prototypes \
	-Wstrict-prototypes \
	-Wold-style-definition \
	-Wmissing-declarations \
	-Wpointer-arith \
	-Wwrite-strings \
	-Wcast-qual \
	-Wcast-align \
	-Wshadow \
	-Wredundant-decls \
	-Wnested-externs \
	-Wno-unused-parameter \
	-Wno-unused-variable \
	-Wno-unused-function

INCLUDES = -I include
