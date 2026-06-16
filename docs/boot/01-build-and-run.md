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

QEMU 默认内存是 512M，普通运行和 `make test` 共用同一个 `QEMU_MEMORY` 参数。需要
模拟更大内存时可以覆盖：

```sh
make run QEMU_MEMORY=2G
make test QEMU_MEMORY=2G
```

退出 QEMU 控制台：

```text
Ctrl-A，然后 X
```

## 自动测试

```sh
make test
```

`make test` 是 CI 和本地共用的全流程入口。当前它会依次完成：

- 构建 EFI app。
- 用 `KERNEL_SELFTEST` 宏构建测试版 kernel ELF。
- 检查测试版 kernel ELF 是否还有未定义符号。
- 生成测试版 ESP 镜像。
- 启动 QEMU/EDK2，等待内核日志出现 `Kernel selftest passed`。

当前内核自检覆盖：

- `trap`：固定 32-bit `ebreak` 进入 trap，handler 推进 `sepc` 后返回。
- `timer`：注册 1ms 一次性事件和周期事件，等待 timer interrupt 回调触发，再取消
  周期事件。
- `page_alloc`：基本分配释放、double free 错误路径、单页分配直到耗尽、耗尽后失败
  返回、释放后计数恢复。
- `vm`：映射、查询、解除映射、冲突映射失败回滚。
- `kmalloc`：`kzalloc` 清零、小块批量分配/释放/复用、大块分配、扩堆直到耗尽。

内核现在还没有关机或退出系统调用，所以测试脚本不会等程序自然退出。它会在日志出现
目标标记后主动结束 QEMU，并把完整日志保存在：

```text
build/test/qemu-selftest.log
```

`make test` 默认最多等待 45 秒，超过时间还没看到目标标记就判定失败，并打印完整 QEMU
日志。需要临时放宽时可以覆盖：

```sh
make test QEMU_TEST_TIMEOUT=120
```

测试版产物保留在：

```text
build/test/kernel/kernel.elf
build/test/esp.img
```

如果要上板运行同一套自检，可以把 `build/test/kernel/kernel.elf` 放到启动介质的
`RVOS/KERNEL.ELF`。普通 `make all` / `make efi-esp` 的产物仍然放在 `build/kernel`
和 `build/efi`，不会被 `make test` 覆盖。

后续文件系统、用户态或系统调用测试也应该挂到 `make test` 下面。这样 CI、本地和开发
容器只需要维护同一条测试入口。

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

## TFTP 更新开发板文件

频繁上板调试时，不需要反复插拔 SD 卡。主机用 TFTP 暴露最新构建产物，U-Boot 从
网口下载文件，再写回 SD 卡的 FAT 分区。

主机侧先生成并刷新 TFTP 目录：

```sh
make tftp-sync
```

默认会写入：

```text
/tmp/rvos-tftp/BOOTRISCV64.EFI
/tmp/rvos-tftp/KERNEL.ELF
```

然后启动临时 TFTP 服务：

```sh
make tftp-serve
```

默认参数按当前调试机器设置：

```text
TFTP_IFACE=enp55s0
TFTP_HOST=10.90.50.43
TFTP_ROOT=/tmp/rvos-tftp
```

如果网卡或地址不同，可以覆盖：

```sh
make tftp-serve TFTP_IFACE=enp55s0 TFTP_HOST=10.90.50.43
```

U-Boot 侧先配置网络：

```text
setenv serverip 10.90.50.43
setenv ipaddr 10.90.50.44
setenv netmask 255.255.255.0
ping ${serverip}
```

确认 `ping` 成功后，下载并覆盖 SD 卡中的 EFI app 和 kernel ELF：

```text
tftpboot 0x88000000 BOOTRISCV64.EFI
fatrm mmc 1:1 /EFI/BOOT/BOOTRISCV64.EFI
fatwrite mmc 1:1 0x88000000 /EFI/BOOT/BOOTRISCV64.EFI ${filesize}

tftpboot 0x88000000 KERNEL.ELF
fatrm mmc 1:1 /RVOS/KERNEL.ELF
fatwrite mmc 1:1 0x88000000 /RVOS/KERNEL.ELF ${filesize}
```

最后按原路径启动：

```text
fatload mmc 1:1 0x80200000 /EFI/BOOT/BOOTRISCV64.EFI
bootefi 0x80200000 ${fdtcontroladdr}
```

可以把上面这段固化成 U-Boot 环境变量，之后只执行一个命令：

```text
setenv rvos_update_boot 'tftpboot 0x88000000 BOOTRISCV64.EFI; fatrm mmc 1:1 /EFI/BOOT/BOOTRISCV64.EFI; fatwrite mmc 1:1 0x88000000 /EFI/BOOT/BOOTRISCV64.EFI ${filesize}; tftpboot 0x88000000 KERNEL.ELF; fatrm mmc 1:1 /RVOS/KERNEL.ELF; fatwrite mmc 1:1 0x88000000 /RVOS/KERNEL.ELF ${filesize}; fatload mmc 1:1 0x80200000 /EFI/BOOT/BOOTRISCV64.EFI; bootefi 0x80200000 ${fdtcontroladdr}'
saveenv
```

以后主机侧运行 `make tftp-sync`，板子侧运行：

```text
run rvos_update_boot
```

## 显示输出实验结论

当前上板调试验证过 U-Boot 的 `lcdputs` 和 UEFI GOP framebuffer 在
`ExitBootServices()` 前可以显示内容。但退出 Boot Services 后，显示器会进入节能
状态，说明固件没有继续保证显示控制器链路可用。因此当前启动主线不把 GOP
framebuffer 作为内核输出接口，内核日志仍以 UART/printk 为准。

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
