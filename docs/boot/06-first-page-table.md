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

这张页表不是最终地址空间设计，但已经开始拆基本权限：

```text
kernel text    : R/X
kernel rodata  : R
kernel data/bss: R/W
kernel stack   : R/W
boot_info      : R/W
memory map/DTB : R
usable ranges  : R/W
```

链接脚本会把关键 section 按 4KB 对齐，因为同一物理页只能有一条 PTE。后面接入
设备 MMIO、direct map 和用户态地址空间后，还要继续细化映射范围和权限。

PTE 里的 `A` 和 `D` 分别表示“已访问”和“已写入”。它们不是硬件自动清零的 cache
状态；通常是硬件或 OS 置位，OS 在需要统计访问/写入情况时主动清零。当前还没有
缺页异常处理，所以早期页表会预先置 `A`，并对可写页预先置 `D`，避免第一次访问或
写入时因为 A/D 位缺失而进入无法处理的 page fault。

## 页表页数量

日志里的 `page_table_pages` 是页表自身占用的 4KB 页数，不是映射了多少内存。

Sv39 支持 1GB、2MB 和 4KB 三种层级的叶子映射，所以映射很大一段物理内存时不一定
需要一页一页铺满页表。

根页表没有固定物理地址。内核会从当前物理页分配器里申请一页作为根页表，然后把
它的物理页号写进 `satp`：

```text
root_table=0x0000000080060000
```

这个地址来自 EFI memory map 里的可用 `ConventionalMemory`。在 QEMU 当前日志里，
OpenSBI 固件位于 `0x80000000` 附近，EDK2 给出的可用内存从 `0x80060000` 开始；
kernel ELF 则装载在 `0x80200000`。因此根页表页位于 OpenSBI 之后、kernel 之前，
不会和二者冲突。后续真实硬件也应以最终 EFI memory map 为准，而不是手写猜测某段
物理地址是否空闲。

页表的中间层不是预先放在固定数组里，而是在建映射时按需申请。`ensure_next_table()`
的职责是保证“还没到叶子 level 时，下一级页表一定存在”：

```text
1GB leaf, level=2:
  直接在根页表写叶子 PTE，不需要下一级页表

2MB leaf, level=1:
  确保 root -> level1 table 存在，再在 level1 写叶子 PTE

4KB leaf, level=0:
  确保 root -> level1 -> level0 table 存在，再在 level0 写叶子 PTE
```

所以大页选择发生在 `identity_map_range()`，它根据地址对齐和剩余长度决定传给
`map_leaf()` 的 level；`ensure_next_table()` 只负责把到达目标 level 之前的页表
补齐。如果某个区域已经被更细的小页映射占用，代码会拒绝再用大页覆盖它，避免丢失
已有权限。

## 验证

启动日志里出现下面两行，说明内核已经用自己的页分配器准备页表，并成功打开 Sv39：

```text
Kernel page table prepared
Kernel Sv39 enabled
```

第二行是在写入 `satp` 之后打印的。如果这行还能输出，说明当前代码、栈、SBI 输出
路径和必要数据范围都已经被新页表覆盖。
