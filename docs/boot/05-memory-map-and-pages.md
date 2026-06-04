# Memory Map 与物理页分配

EFI loader 退出 Boot Services 后，RVOS 不能再依赖固件分配内存。内核必须根据
`boot_info` 里的最终 EFI memory map 建立自己的物理页分配器。

这一页按内核入口里的调用链阅读：

```text
kernel_entry()
  -> memory_probe()：解析最终 EFI memory map，整理 usable/reserved ranges。
  -> page_allocator_init()：用 bitmap/refcount 接管可用物理页。
```

## memory_probe()

`memory_probe()` 的输入是 `boot_info` 中的最终 EFI memory map。每条 EFI descriptor
描述一段物理内存的类型、物理起始地址、页数和属性。

当前内核只把 `EfiConventionalMemory` 作为可分配物理页来源。这个选择偏保守：退出
Boot Services 后，部分 Boot Services 内存理论上可以回收，但其中可能混有启动阶段
仍要保留的材料。第一版先只拿最确定安全的内存。

`memory_probe()` 输出两类区间：

```text
usable ranges   : 当前允许交给物理页分配器的 ConventionalMemory
reserved ranges : kernel image、boot_info、最终 memory map、初始内核栈
```

reserved ranges 不会进入页分配器。否则内核可能把正在执行的代码、启动参数、memory
map 或当前栈重新分配出去。

## 为什么 descriptor_size 必须保存

UEFI memory descriptor 未来可以扩展。内核遍历 memory map 时不能写死
`sizeof(struct efi_memory_descriptor)`，必须按固件返回的 `descriptor_size` 前进。

这就是 `boot_info` 同时记录下面字段的原因：

```text
efi_memory_map_phys
efi_memory_map_size
efi_descriptor_size
efi_descriptor_version
```

## page_allocator_init()

`page_allocator_init()` 消费 `memory_state()->usable_ranges`，建立 4KB 物理页分配器。

当前实现使用 bitmap/refcount：

```text
free_bitmap bit=1 : 该 PFN 当前空闲
free_bitmap bit=0 : 该 PFN 不可用或已分配
refcount=0        : 未分配
refcount=1        : 已分配
```

metadata 本身也放在物理内存里。初始化顺序是：

```text
1. 根据 usable ranges 找出 PFN 覆盖范围。
2. 计算 bitmap/refcount 需要多少字节。
3. 从 usable memory 中找一段足够大的连续空间保存 metadata。
4. 把所有 usable ranges 标为空闲。
5. 再把 metadata 自己占用的页标成已占用。
```

metadata 不放在内核镜像的固定数组里。因此物理内存变大时，页管理元数据也可以随之
变大。最近 QEMU 日志里可以看到：

```text
metadata_phys=0x0000000080060000
metadata_pages=0x000000000000010e
```

也就是页管理元数据用了 `0x10e` 个 4KB 页。这已经超过“小固定数组”的规模，但仍然
安全，因为它来自 EFI memory map 中的 usable memory。

## 空洞和 PFN 覆盖范围

bitmap 覆盖的是所有 usable ranges 的最小 PFN 到最大 PFN。中间可能存在不可用空洞。
这些洞不会被 `mark_usable_range()` 标为空闲，因此 bitmap 可以覆盖它们，但不会把
洞误分配出去。

这种做法比 sparse memory 简单，适合当前 QEMU 和开发板规模。后续如果物理地址空间
极度稀疏，可以改成按 region/zone 分段管理。

## 分配和释放

`phys_alloc_pages(pages)` 查找连续空闲页，成功后把对应 bitmap 位清零，并把 refcount
设为 1。

`phys_free_pages(addr, pages)` 会拒绝：

- 未对齐地址。
- 不在管理范围内的地址。
- double free。
- 释放未分配页。
- 越界释放。

释放成功后，bitmap 位重新置 1，refcount 清零。

## 当前边界

这个页分配器已经不受“空闲区间数量”限制，能支撑页表页、`kmalloc` 后端和第一版用户
页分配。

它还不是最终 buddy allocator：

- refcount 当前只表达是否分配，不表达共享页或 COW。
- 连续页分配使用线性 first-fit 扫描。
- 没有 zone、NUMA、页归属、页表页回收。

这些能力可以在接口稳定后继续补，不需要回退到 EFI 或早期区间表。
