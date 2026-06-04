# 第一张 Sv39 页表

内核自己的第一张页表不应该再向 EFI 申请内存。退出 Boot Services 后，EFI 分配接口
已经失效，页表页必须从 RVOS 自己的物理页分配器里拿。

当前调用链是：

```text
early_vm_enable()
  -> vm_space_create()：分配根页表。
  -> vm_identity_map()：映射内核 section、启动材料和可用物理内存。
     -> vm_map_range()：按范围建立 VA 到 PA 的映射。
        -> ensure_next_table()：按需分配中间页表页。
        -> map_leaf()：写入 1GB、2MB 或 4KB 叶子 PTE。
  -> vm_activate_sv39()：写 satp，启用 Sv39。
```

## early_vm 和 vm

`early_vm` 只负责选择启动后必须立刻可访问的范围。真正页表操作在 `vm.c` 中。

需要映射的范围包括：

```text
kernel text / rodata / data / bss
initial kernel stack
boot_info
final EFI memory map
DTB
usable physical ranges
```

## 恒等映射

当前页表采用恒等映射：

```text
virtual address == physical address
```

这样打开 `satp` 后，正在执行的代码、当前栈、启动信息和后续新分配的物理页仍然能
被内核直接访问。

这个阶段还没有正式 direct map，也没有 `phys_to_virt()` / `virt_to_phys()` 边界。
恒等映射是从 EFI 交接环境过渡到内核自管页表的最直接方案。

## section 权限

第一张页表已经拆基本权限：

```text
kernel text     : R/X
kernel rodata   : R
kernel data/bss : R/W
kernel stack    : R/W
boot_info       : R/W
memory map/DTB  : R
usable ranges   : R/W
```

链接脚本会把关键 section 按 4KB 对齐。原因是同一物理页只能有一条 PTE；如果 text
和 rodata/data 混在同一页，就无法可靠地拆权限。

## PTE A/D 位

RISC-V Sv39 PTE 里的 `A` 和 `D` 分别表示 accessed 和 dirty。

它们不是 cache 那样自动清零的状态位。RISC-V 允许两种实现：

- 硬件自动把 A/D 置 1。
- 硬件在 A/D 缺失时触发 page fault，让 OS 手动补位。

当前还没有 page fault handler，所以创建映射时预先置 `A`，并对可写页预先置 `D`。
这样第一次取指、读 rodata、写 bss/栈不会因为 A/D 位缺失进入无法处理的异常。

## 页表页按需分配

页表页不是固定数组，也不是启动时一次性预留固定数量。

`vm_space_create()` 只分配一页根页表。后续建立映射时，如果某一级页表不存在，
`ensure_next_table()` 会调用 `phys_alloc_pages(1)` 按需分配页表页。

因此运行时继续映射 MMIO、vmalloc 或用户页时，只要物理页分配器还能分出页，页表就
可以继续增长。

## 大页和小页

Sv39 支持不同层级的叶子映射：

```text
level 2 leaf : 1GB
level 1 leaf : 2MB
level 0 leaf : 4KB
```

`vm_map_range()` 会优先使用能覆盖当前地址的最大页大小。地址和剩余长度满足 1GB
对齐时用 1GB leaf；否则尝试 2MB；最后退回 4KB。

`ensure_next_table()` 不决定页大小。它只保证在到达目标 leaf level 前，下一级页表
存在。

当前还不支持把已有大页拆成小页。如果某个区域已经被 1GB/2MB leaf 映射，后续想只改
其中一个 4KB 页，代码会拒绝。这个能力等用户地址空间和 page fault 接入后再补。

## 启用 satp

根页表没有固定物理地址。内核从页分配器申请一页作为根页表，然后把物理页号写进
`satp`：

```text
satp = (SV39_MODE << 60) | (root_phys >> 12)
```

写 `satp` 前后都会执行 `sfence.vma`，确保旧地址转换缓存不影响新页表。

启动日志里出现：

```text
Kernel page table prepared
Kernel Sv39 enabled
```

说明内核已经用自己的物理页分配器准备页表，并成功打开 Sv39。第二行是在写入 `satp`
之后打印的；如果它还能输出，说明代码、栈和 SBI 输出路径都已经被新页表覆盖。
