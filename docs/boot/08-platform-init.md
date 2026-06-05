# 平台 MMIO 与 UART 输出

DTB 解析完成后，内核已经知道当前平台上 UART、时钟频率和中断控制器的位置。但
“知道地址”和“能访问地址”不是一回事：此时 Sv39 已经启用，内核只能访问当前页表
里有映射的地址。

## 调用链

```text
kernel_entry()
  -> dtb_init()：解析 DTB，填充 platform_info。
  -> platform_map_mmio()：把 UART/PLIC MMIO range 追加进当前内核页表。
     -> vm_identity_map()：建立 VA == PA 的设备映射。
        -> vm_map_range()：把起点和长度扩成页粒度范围。
           -> map_range_inner()：选择 1GB/2MB/4KB 叶子映射。
              -> map_leaf()：实际写入 PTE。
     -> vm_flush_all()：刷新地址转换缓存。
  -> printk_init()：用 platform_info.uart_base 初始化 UART backend。
```

## 为什么 DTB 后还要映射 MMIO

`early_vm_enable()` 只映射启动后立即需要的内存：

- 内核 text/rodata/data/bss；
- 内核栈；
- `boot_info`；
- EFI memory map；
- DTB；
- EFI conventional memory 中整理出的可用物理内存。

UART 和 PLIC 属于设备 MMIO，不属于 EFI conventional memory。如果直接在开启 MMU 后
访问 `0x10000000` 这样的 UART 地址，页表里没有对应 PTE，就会触发访问异常。当前
trap 还没正式接管，所以表现通常是没有后续输出或直接卡住。

因此顺序必须是：

```text
解析 DTB 得到 MMIO 地址
  -> 把 MMIO 地址映射进内核页表
  -> 再初始化 UART/printk
```

## early_log 和 printk 的边界

`early_log` 固定使用 SBI console。它的职责是覆盖最早期阶段：DTB 还没解析、UART
还不知道在哪里、页表也可能还没完全准备好。

`printk` 是正式内核日志入口。`printk_init()` 会优先使用 `platform_info` 中的 UART
地址；如果 UART 不可用，仍然保留 SBI fallback。后续 trap、timer、调度器和内存
管理都应该调用 `printk`，不要直接依赖 SBI。

## 当前 UART 实现范围

当前 UART 驱动只实现 ns16550 兼容设备的最小发送路径：

- 等待 `LSR.THRE` 表示发送保持寄存器为空；
- 写 `THR` 输出一个字符；
- 不重新配置波特率；
- 不开启 UART 中断；
- 不处理输入。

这足够让内核在 QEMU 和当前开发板启动阶段接管日志输出。完整串口驱动后续可以继续
扩展 FIFO、中断输入和控制台抽象。
