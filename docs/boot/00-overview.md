# 启动流程总览

RVOS 当前默认走 EFI 启动路径。这个总览只说明启动链边界和阅读顺序；EFI 文件读取、
ELF 装载、memory map、页分配器和页表细节分别在后续页面展开。

## 时间线

```text
OpenSBI
  -> U-Boot bootefi / QEMU EDK2
  -> RVOS EFI app
  -> RVOS/KERNEL.ELF
  -> kernel_entry(boot_info)
```

OpenSBI 负责把机器带到 S-mode，并提供 SBI 调用。U-Boot 或 EDK2 提供 UEFI Boot
Services。RVOS EFI app 只负责启动交接：找到 DTB、读取 memory map、装载 kernel
ELF、准备 `boot_info`、退出 Boot Services。

退出 Boot Services 后，固件分配接口失效。内核入口收到 `boot_info`，校验启动 ABI，
整理 memory map，建立物理页分配器，打开第一张 Sv39 页表。

## 边界

```text
固件负责启动服务
EFI loader 负责交接材料
kernel ELF 负责长期运行
```

后续 trap、设备驱动、文件系统、用户程序加载和 Linux syscall 兼容，都建立在这条
边界清楚的启动链之上。

## 阅读顺序

- [01 构建与运行](01-build-and-run.md)
- [02 EFI Loader](02-uefi-loader.md)
- [03 Boot Info](03-boot-info.md)
- [04 Kernel Entry](04-kernel-entry.md)
- [05 Memory Map 与物理页分配](05-memory-map-and-pages.md)
- [06 第一张 Sv39 页表](06-first-page-table.md)
- [07 内存接口](07-memory-interfaces.md)
- [08 平台 MMIO 与 UART 输出](08-platform-init.md)
- [09 Trap 入口与返回](09-trap-entry.md)
- [10 SBI Timer 与系统 Tick](10-sbi-timer.md)
- [11 内核 Task 与上下文切换](11-kernel-task.md)
- [12 IRQ 与串口输入](12-irq-console-input.md)
- [13 Spinlock 与中断状态](13-spinlock.md)
