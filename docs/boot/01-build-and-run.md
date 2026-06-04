# 构建与运行

这一页记录 RVOS 当前 EFI 启动路径下的构建产物、运行命令和上板方式。快速开始放在
项目根目录 [README](../../README.md)，这里保留细节。

## 工具链

默认编译器是 Clang/LLVM：

```sh
make toolchain
```

EFI app 最后一步仍会使用 GNU objcopy 做 PE/COFF 格式转换。原因是当前
`llvm-objcopy` 不支持 `efi-app-riscv64` 输出格式。Clang 负责 C 编译和 lld 链接，
GNU objcopy 只负责把 EFI ELF 中间产物转换成 RISC-V EFI 应用。

## 默认产物

```sh
make
```

生成：

```text
build/efi/BOOTRISCV64.EFI
build/kernel/kernel.elf
```

`BOOTRISCV64.EFI` 是 UEFI 默认路径下的 RISC-V EFI 应用。`kernel.elf` 是真正长期
运行的 RVOS 内核。

## ESP 镜像

```sh
make efi-esp
```

生成：

```text
build/efi/esp.img
```

镜像内容：

```text
EFI/BOOT/BOOTRISCV64.EFI
RVOS/KERNEL.ELF
```

EFI loader 启动后会从同一个启动介质打开 `RVOS/KERNEL.ELF`。

## QEMU

```sh
make run
```

`make run` 使用 QEMU virt 机器和 EDK2 固件。当前显式关闭 ACPI：

```text
-machine virt,acpi=off
```

这样 EDK2 会把 QEMU 构造的 DTB 注册进 EFI configuration table，行为更接近开发板
上 U-Boot `bootefi` 传 DTB 的方式。

QEMU 保留 4 个 hart。启动日志里的 boot hart 不一定是 0，内核必须使用
`boot_info->boot_hart_id`。

退出 QEMU 控制台：

```text
Ctrl-A，然后 X
```

## 上板启动

把 EFI 应用放在 FAT32/ESP 分区的 UEFI 默认启动路径：

```text
EFI/BOOT/BOOTRISCV64.EFI
```

同时把内核 ELF 放在：

```text
RVOS/KERNEL.ELF
```

U-Boot 手动启动示例：

```text
fatload mmc 1:1 0x80200000 /EFI/BOOT/BOOTRISCV64.EFI
bootefi 0x80200000
```

如果固件没有自动把 DTB 放进 EFI configuration table，可以显式传 DTB：

```text
bootefi 0x80200000 ${fdtcontroladdr}
```

当前 StarFive U-Boot 已经能通过 EFI configuration table 提供 DTB，是否需要第二个
参数以实际固件行为为准。

## 独立目标

只构建 kernel ELF：

```sh
make kernel
```

只构建 EFI app：

```sh
make efi
```

显示 ELF 未定义符号：

```sh
make check-undef
```

裸机环境没有动态链接器，也不会在运行时补 libc。Clang/GCC 有时会生成 helper 调用，
例如 `memcpy`、`memset`、`__udivdi3`、`__atomic_*`。`make check-undef` 没有输出，
才表示最终 kernel ELF 没有未定义符号。
