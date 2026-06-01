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
并读取 UEFI memory map，打印每条 descriptor 的类型、物理起始地址、页数和属性。

EFI 启动代码放在 `boot/efi/`。其中 `boot/efi/include/` 保存最小 UEFI ABI 声明，
`boot/efi/boot/` 保存当前 EFI 应用的实现。Makefile 会扫描 `boot/efi/boot/*.c`，
新增 EFI 启动源文件时不需要手工维护对象列表。

## EFI 阶段的内存分配

EFI 阶段确实需要调用固件提供的分配接口，但它只应该服务于启动器自己。

`GetMemoryMap` 用来向固件索取当前物理内存布局。返回结果是一组 descriptor，每条
记录描述一段物理内存的类型、起始地址、页数和属性。RVOS 后续建立物理页分配器
时，应以最后保存下来的 memory map 为主要依据：普通可用内存才能进入空闲页链表，
MMIO、Runtime Services、保留区域和启动器明确占用的页都不能直接复用。

`AllocatePool` 可以理解成 EFI 阶段的临时 `malloc`。它适合保存路径字符串、临时
文件缓冲区、探测阶段的 memory map buffer 这类小对象。它不强调页对齐，也不适合
承载内核镜像。

`AllocatePages` 可以理解成 EFI 阶段的物理页分配。内核 ELF 的加载区域、内核栈、
早期页表、最终要传给内核的 `boot_info` 和最终 memory map，都更适合按页申请。
这样 RVOS 接管后可以直接把这些页标记为已占用，剩余可用页再交给自己的页分配器。

典型用途包括：

```text
读取完整 UEFI memory map 的临时 buffer
加载内核 ELF 时使用的 I/O buffer
为内核镜像选择并占用一段物理内存
构造传给内核的 boot_info
```

这些分配必须发生在 `ExitBootServices` 之前，因为退出之后 Boot Services 全部失效。
内核一旦接管，就不能再调用 EFI 的 `AllocatePool` 或 `AllocatePages` 来做普通内存
管理。RVOS 自己的页分配器、堆分配器和用户态内存布局，仍然要基于启动阶段保存
下来的 memory map 自己建立。

因此 EFI 启动器里的 allocate 代码应该很窄：只准备启动所需材料，把结果写入
`boot_info`，然后退出 EFI 并跳进内核。

`GetMemoryMap` 返回的 `map_key` 是当前 memory map 的版本凭证。只要还在调用
`AllocatePool`、`AllocatePages`、`FreePool` 或 `FreePages`，固件内部的内存表就
可能变化，旧的 `map_key` 就可能失效。`ExitBootServices(image_handle, map_key)`
要求传入最后一次成功读取 memory map 时得到的 key；如果 key 过期，固件会拒绝
退出 Boot Services。

因此正式启动流程应该遵守这个顺序：

```text
完成所有必须保留的分配
释放所有临时 buffer
最后一次 GetMemoryMap，保存最终 memory map 和 map_key
立刻 ExitBootServices
跳进 RVOS 内核入口
```

当前 EFI loader 已经会执行最小控制权交接。它先打印探测信息，然后释放临时
memory map，重新读取最终 memory map，立刻 `ExitBootServices`，切到初始内核栈，
再 jump 到内置 kernel stub。stub 会通过 SBI debug console 打印一行
`RVOS kernel stub entered` 后停住；真正内核加载还没有接入。

这里有两份 memory map。第一份只用于调试打印，打印完会释放；释放本身会改变
memory map，所以不能拿它去 `ExitBootServices`。第二份是在交接前重新读取的最终
memory map，它不会释放，而是写进 `boot_info` 留给内核。`boot_info` 页和初始
kernel stack 页同样不会在成功路径释放，内核接手后要把这些页视为已占用。

当前实现已经开始构造 `boot_info`。这个结构定义在 `include/rvos/boot_info.h`，
它是 EFI loader 和 RVOS 内核之间的启动 ABI。第一版只放固定字段，不做变长数组：

```text
magic / version / size / flags
DTB 物理地址和大小
EFI memory map 地址、大小、descriptor size、descriptor version
boot hart id
内核镜像地址和大小
boot_info 自己的地址和大小
初始内核栈地址和大小
```

初始内核栈当前为 32KB。它只用于进入内核后的早期初始化，后续线程栈、进程栈和
中断栈仍然应该由 RVOS 自己管理。

其中 `descriptor size` 不能省略。UEFI 允许 memory descriptor 在未来扩展，遍历
memory map 时必须按固件返回的 descriptor size 前进，而不是按当前 C 结构体的
`sizeof` 前进。

RISC-V boot hart id 只通过 `RISCV_EFI_BOOT_PROTOCOL.GetBootHartId()` 获取。这里
不从 DTB 猜，也不默认写 0；如果固件不提供规范协议，就让启动阶段明确失败。

从 EFI loader 跳到内置 stub 时不走普通 C 函数调用，而是走
`boot/efi/boot/jump.S`。RISC-V EFI app 现在按 PIC 方式构建，C 里取函数指针或汇编
里使用普通 `la` 可能经由 GOT。当前 PE/COFF 生成链路还没有完整处理这类动态重定位，
所以跳转目标不能依赖 GOT。`jump.S` 使用 `lla` 生成 PC-relative 地址：

```asm
lla t0, rvos_kernel_stub
jr t0
```

这样切栈后跳到 stub 不依赖未处理的 GOT 项。

## EFI 服务是谁提供的

EFI 应用不是直接调用 libc，也不是调用 RVOS 内核。`efi_main` 收到的
`system_table` 里保存了一组函数指针，例如 console 输出、memory map 查询、pool
分配和页分配。EFI 应用调用这些函数指针，实际执行者是当前启动固件。

在开发板上，这些服务来自 U-Boot 的 UEFI implementation：

```text
OpenSBI -> U-Boot -> bootefi -> RVOS EFI app
```

在 QEMU 上，这些服务来自 EDK2：

```text
OpenSBI -> EDK2 -> Boot Manager -> RVOS EFI app
```

UEFI 约定保证了同一个 EFI app 可以在这两条链路上运行，但不同实现仍可能有差异。
例如 QEMU/EDK2 默认偏向 ACPI，只有 `make run` 显式关闭 ACPI 后，EDK2 才会把 QEMU
构造的 DTB 注册进 EFI configuration table。

## 多 hart 启动

EFI app 一般只在 boot hart 上运行。机器可以有多个 hart，但固件会选择一个 hart
进入下一阶段，其他 hart 不会同时跑 `efi_main`。

QEMU 日志里的：

```text
Platform HART Count : 4
Boot HART ID        : 1
```

表示机器有 4 个 hart，当前启动流程跑在 hart 1 上。后续 EFI 启动层需要把 boot
hart id 整理进 `boot_info`，RVOS 内核据此让主 hart 执行初始化，再通过 SBI HSM
等机制启动其它 hart。

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

`make run` 会走 QEMU/EDK2，启动默认 EFI 应用。旧的 `-kernel build/os.elf` 路径
保留为 `make run-image`，用于临时验证旧裸内核。

QEMU 的 EFI 路径会显式关闭 ACPI：

```text
-machine virt,acpi=off
```

这样 EDK2 会把 QEMU 构造的 DTB 注册进 EFI configuration table，行为更接近当前
板子上的 U-Boot `bootefi`。`make run` 同时保留 4 个 hart，启动日志里的 boot hart
不一定是 0，后续 EFI 启动层需要把“当前主核是谁”整理进传给内核的启动信息。

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
