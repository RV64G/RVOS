# 构建与运行：从裸镜像转向 EFI 应用

RVOS 早期的构建产物是一个裸 `Image`，再配合 `boot.scr` 交给 U-Boot：

```text
fatload mmc 1:1 0x80200000 Image
go 0x80200000
```

这条路很直接，但它也把很多启动细节压回了内核自己身上。内核入口需要假定加载
地址、准备 C 运行环境、接收设备树、再自己一点点把启动信息整理出来。既然目标
板子和 QEMU 都已经能运行 RISC-V EFI 应用，新的默认构建就从裸 `Image` 转向
`BOOTRISCV64.EFI`。

以后 `make` 代表“生成当前默认启动方式的产物”。旧裸镜像仍然保留，但必须显式
指定 `make image`。这样默认路径和后续开发方向保持一致。

## 安装依赖

依赖清单放在 `tools/deps/`，脚本放在 `scripts/`。清单只回答“需要装什么”，脚本
回答“当前发行版怎么装”。

CachyOS/Arch Linux：

```sh
./scripts/install-deps-arch.sh
```

Ubuntu/Debian：

```sh
./scripts/install-deps-ubuntu.sh
```

EFI 构建仍然以 Clang/LLVM 为主，但当前 `llvm-objcopy` 不支持
`efi-app-riscv64` 输出格式，所以最后一步会使用 GNU binutils 里的
`riscv64-elf-objcopy` 或 `riscv64-unknown-elf-objcopy` 把 ELF 中间产物转换成
RISC-V EFI PE/COFF 应用。

## 默认构建什么

默认命令：

```sh
make
```

会生成：

```text
build/efi/BOOTRISCV64.EFI
```

这个文件应该放到 FAT32/ESP 分区的 fallback 路径：

```text
EFI/BOOT/BOOTRISCV64.EFI
```

如果是在 U-Boot 命令行里手动启动，可以先加载再 `bootefi`：

```text
fatload mmc 1:1 0x80200000 /EFI/BOOT/BOOTRISCV64.EFI
bootefi 0x80200000
```

当前默认 EFI 应用不是单纯的 hello world，而是 EFI 启动层的最小形态。它会通过
UEFI console 打印 RVOS EFI boot 信息，尝试从 UEFI configuration table 中查找 DTB，
并查询 UEFI memory map 的大小、描述符大小和条目数量。

## 旧镜像怎么构建

旧的 OpenSBI/U-Boot 裸镜像路径还没有立刻删除。需要验证旧内核时显式运行：

```sh
make image
```

它会生成：

```text
build/os.elf
Image
boot.scr
os.txt
```

`make run`、`make debug`、`make qemu-gdb-server` 仍然走旧镜像，因为它们目前使用
QEMU 的 `-kernel build/os.elf` 路径。等 EFI loader 能真正进入内核后，这些目标
再迁到 EFI 启动方式。

## 为什么保留 GCC 对照

默认编译器是 Clang：

```sh
make toolchain
```

旧镜像可以用 GCC 对照构建：

```sh
make image TOOLCHAIN=gcc
```

保留 GCC 不是因为 EFI 必须用 GCC，而是因为 RISC-V 裸机开发常常需要 GNU
binutils。当前 EFI app 的最后转换步骤也依赖 GNU `objcopy` 支持
`efi-app-riscv64`。Clang 负责 C 编译和 lld 链接，GNU objcopy 只负责格式转换。

## 源码如何自动参与构建

内核源码列表由 `mk/sources.mk` 按目录扫描生成。新增文件时不需要手写每个
`.c`/`.S` 文件名，只要放在约定目录即可：

```text
kernel/*.c
mm/*.c
drivers/*.c
user/*.c
test/*.c
arch/riscv/*.S
```

扫描不是递归全仓库，而是目录白名单。这样可以避免把 host-side 工具、临时实验
代码或未来的生成器源码误编进内核。

`arch/riscv/start.S` 是例外，它会固定排在旧镜像汇编对象列表最前面。ELF 入口虽
然由 `ENTRY(_start)` 指定，但旧裸 `Image` 配合 `go 0x80200000` 时仍然需要镜像
开头就是启动入口。

## check-undef 检查什么

裸机环境没有动态链接器，也不会在运行时帮内核补 libc。Clang/GCC 有时会因为某些
C 写法生成 helper 调用，例如：

```text
memcpy
memset
__udivdi3
__atomic_*
```

这些符号如果留在最终 ELF 里，启动后没有地方解析。`make check-undef` 会对旧内核
ELF 执行 `nm -u`：

```sh
make image
make check-undef
```

没有输出才表示最终 `build/os.elf` 没有未定义符号。

## 链接脚本还管什么

`os.ld` 仍然服务于旧内核镜像。它指定 `_start` 入口、链接地址、段布局，并导出
`_text_start`、`_bss_start`、`_memory_end` 等链接期符号。

EFI 默认产物不走 `os.ld`。EFI 应用使用自己的 PE/COFF 生成链路：

```text
C source
  -> clang 生成 RISC-V ELF object
  -> lld 链接成 ELF shared object
  -> GNU objcopy 转成 efi-app-riscv64
```

等后续把 EFI app 和内核真正接起来时，会重新设计 EFI loader 与内核之间的入口
ABI。旧 `os.ld` 要么退役，要么只用于独立内核 ELF。

## 建议验证命令

构建系统改动后，至少跑：

```sh
make clean && make all && make efi-info
make clean && make image && make check-undef
make clean && make image TOOLCHAIN=gcc
```

预期结果：

```text
build/efi/BOOTRISCV64.EFI: PE32+ executable for EFI (application), RISC-V 64-bit
```

旧镜像仍会有 RWX LOAD segment 警告，这是当前简化链接布局造成的。它属于旧路径
问题，不影响 EFI 启动产物。
