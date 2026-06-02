# 构建与运行

RVOS 早期曾经走裸 `Image` 加 U-Boot 脚本的启动方式。这条路很直接，但它也把很多
启动细节压回了内核自己身上：内核入口需要假定加载地址、准备 C 运行环境、接收
设备树、再自己整理启动信息。既然目标板子和 QEMU 都能运行 RISC-V EFI 应用，当前
仓库只保留 EFI 启动路径。

## 安装依赖

依赖清单放在 `tools/deps/`，脚本放在 `scripts/`。清单回答“需要装什么”，脚本回答
“当前发行版怎么装”。

CachyOS/Arch Linux：

```sh
./scripts/install-deps-arch.sh
```

Ubuntu/Debian：

```sh
./scripts/install-deps-ubuntu.sh
```

默认编译器是 Clang/LLVM。当前 `llvm-objcopy` 不支持 `efi-app-riscv64` 输出格式，
所以 EFI app 最后一步会使用 GNU binutils 里的 `riscv64-elf-objcopy` 或
`riscv64-unknown-elf-objcopy` 把 ELF 中间产物转换成 RISC-V EFI PE/COFF 应用。

## 默认产物

默认命令：

```sh
make
```

会生成：

```text
build/efi/BOOTRISCV64.EFI
build/kernel/kernel.elf
```

前者是 EFI 应用，后者是真正的 RVOS 内核 ELF。需要生成能直接挂载启动的 ESP 镜像：

```sh
make efi-esp
```

ESP 镜像内会包含：

```text
EFI/BOOT/BOOTRISCV64.EFI
RVOS/KERNEL.ELF
```

## 上板启动

`BOOTRISCV64.EFI` 应放在 FAT32/ESP 分区的 UEFI 默认启动路径：

```text
EFI/BOOT/BOOTRISCV64.EFI
```

如果在 U-Boot 命令行里手动启动，可以先加载再 `bootefi`：

```text
fatload mmc 1:1 0x80200000 /EFI/BOOT/BOOTRISCV64.EFI
bootefi 0x80200000
```

如果需要显式传 DTB，可以把第二个参数传给 `bootefi`。当前 StarFive U-Boot 已经能
通过 EFI configuration table 提供 DTB，常见命令是：

```text
bootefi 0x80200000 ${fdtcontroladdr}
```

## QEMU 启动

QEMU 走 EDK2：

```sh
make run
```

`make run` 会启动 ESP 镜像，并显式关闭 ACPI：

```text
-machine virt,acpi=off
```

这样 EDK2 会把 QEMU 构造的 DTB 注册进 EFI configuration table，行为更接近板子上的
U-Boot `bootefi`。

QEMU 保留 4 个 hart。启动日志里的 boot hart 不一定是 0，内核必须使用 `boot_info`
里的实际 boot hart id。

## 独立构建内核

独立内核 ELF 可以显式构建：

```sh
make kernel
```

## 工具链

默认编译器是 Clang：

```sh
make toolchain
```

EFI 构建仍然会用到 GNU objcopy 做 PE/COFF 格式转换。Clang 负责 C 编译和 lld 链接，
GNU objcopy 只负责把 EFI ELF 中间产物转换成 `efi-app-riscv64`。

## check-undef

裸机环境没有动态链接器，也不会在运行时帮内核补 libc。Clang/GCC 有时会因为某些
C 写法生成 helper 调用，例如：

```text
memcpy
memset
__udivdi3
__atomic_*
```

这些符号如果留在最终 ELF 里，启动后没有地方解析。`make check-undef` 会对当前
`build/kernel/kernel.elf` 执行 `nm -u`：

```sh
make kernel
make check-undef
```

没有输出才表示最终 kernel ELF 没有未定义符号。
