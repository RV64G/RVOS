# 第一张 Sv39 页表

内核自己的第一张页表不应该再向 EFI 申请内存。退出 Boot Services 之后，EFI 的
分配接口已经失效，页表页必须从 RVOS 自己的物理页分配器里拿。

## 恒等映射

当前页表采用 Sv39 恒等映射：

```text
virtual address == physical address
```

它会覆盖这些范围：

```text
kernel image
initial kernel stack
boot_info
final EFI memory map
DTB
usable physical ranges
```

这样打开 `satp` 后，正在执行的代码、当前栈、启动信息和后续新分配的物理页仍然能
被早期内核直接访问。

这个阶段还没有正式的 direct map，也没有区分“物理地址”和“内核虚拟地址”的接口
边界，所以恒等映射是最直接的过渡方案。

## 权限

这张页表不是最终地址空间设计。权限先给成 R/W/X，是为了先验证控制权交接和地址
转换本身。后面拆出 text/rodata/data、设备 MMIO、direct map 和用户态地址空间后，
再把权限收紧。

## 页表页数量

日志里的 `page_table_pages` 是页表自身占用的 4KB 页数，不是映射了多少内存。

Sv39 支持 1GB、2MB 和 4KB 三种层级的叶子映射，所以映射很大一段物理内存时不一定
需要一页一页铺满页表。

## 验证

启动日志里出现下面两行，说明内核已经用自己的页分配器准备页表，并成功打开 Sv39：

```text
Kernel page table prepared
Kernel Sv39 enabled
```

第二行是在写入 `satp` 之后打印的。如果这行还能输出，说明当前代码、栈、SBI 输出
路径和必要数据范围都已经被新页表覆盖。
