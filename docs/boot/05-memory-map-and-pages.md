# Memory Map 与物理页分配

EFI loader 退出 Boot Services 后，RVOS 不能再依赖固件分配内存。内核必须根据
`boot_info` 里的最终 memory map 建立自己的物理页分配器。

## 启动内存快照

`boot_info` 里的 memory map 是 UEFI 固件给出的物理内存描述。每条 descriptor 说明
一段物理内存的类型、起始地址、页数和属性。

第一版内核只把 EFI `ConventionalMemory` 当成可分配物理页来源。这个选择偏保守：
`LoaderData` 和 `BootServicesData` 在退出 Boot Services 后不一定永远不能用，但
里面混有内核镜像、最终 memory map、`boot_info` 和初始内核栈这些启动保留页。

启动内存快照会保存两类区间：

```text
usable ranges   : 当前允许交给物理页分配器的 Conventional Memory
reserved ranges : 内核镜像、memory map、boot_info、初始内核栈
```

reserved ranges 不会进入 free list。后续如果要回收更多 EFI 类型的内存，应先从对应
区间里扣掉这些启动保留页，再把剩余部分加入正式页分配器。

## 区间表分配器

当前物理页分配器用区间表保存空闲页，而不是一页一个 `Page` 结构：

```text
0x80203000..0x82bf5000
0x83ffe000..0xfe4a4000
...
```

每个区间都是 4KB 页对齐的物理地址范围。分配时采用 first-fit，从第一段足够大的
空闲区间头部切走若干页；释放时把区间插回表里，并尝试和前后相邻区间合并。

这不是最终形态。它适合启动阶段，因为代码短、状态少、容易检查。等到内核需要记录
页所有者、引用计数、slab/kmalloc、用户页表和按需分页时，再切到 bitmap 或 `Page[]`
元数据会更自然。

## 验证

启动日志里出现：

```text
Physical page allocator ready
```

说明内核已经从 EFI memory map 整理出可用页区间，并把它们复制进自己的物理页分配器。
