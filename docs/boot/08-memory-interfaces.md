# 内存接口

内核当前把内存管理拆成三层：

- `boot_memory`：读取 EFI memory map，整理启动期可用和保留的物理范围。
- `page_alloc`：用 bitmap/refcount 管理 4KB 物理页。
- `vm`：操作 Sv39 页表，建立或解除虚拟地址到物理地址的映射。

## 物理页

`page_alloc` 初始化时会从 usable memory 中预留一段空间保存元数据：

- `free_bitmap`：bit 为 1 表示该 PFN 当前空闲。
- `refcounts`：当前只使用 0/1，用来拒绝 double free、释放未分配页和越界释放。

这个实现已经不受“空闲区间数量”限制，能支撑页表页、`kmalloc` 后端和第一版用户页
分配。它还不是 buddy allocator，也没有 COW、zone、NUMA 或页归属信息。

常用接口在 `mm/page_alloc.h`：

- `page_allocator_init()`：从 `boot_memory` 初始化页分配器。
- `page_available_pages()`：返回剩余可分配页数。
- `phys_alloc_pages(pages)`：分配连续物理页。
- `phys_free_pages(addr, pages)`：释放连续物理页。

## 虚拟映射

`vm` 管的是页表，不负责物理页所有权。映射前，调用者应该先通过物理页分配器、
启动信息或设备树确定物理地址。

常用接口在 `mm/vm.h`：

- `vm_space_create()`：创建一张空 Sv39 页表。
- `vm_map_range()`：建立 VA 到 PA 的映射。
- `vm_identity_map()`：建立 VA == PA 的恒等映射。
- `vm_unmap_range()`：清除映射，但不释放物理页。
- `vm_activate_sv39()`：写 `satp` 并启用页表。

基础 `vm_map_range()` 建议按 4KB 页对齐使用。ELF、`mmap` 这类可能涉及非页对齐
offset 的语义，应该由上层 loader 或 VMA 层处理，不塞进最底层页表接口。

## 早期页表

`early_vm` 不是另一套页表实现。它现在只负责选择启动后必须立刻可访问的范围：

- 内核 text / rodata / data / bss。
- 当前内核栈。
- `boot_info`。
- EFI memory map。
- DTB。
- 当前可用物理内存范围。

真正的页表 walk、PTE 权限、1GB/2MB/4KB leaf 选择和 `satp` 切换都在 `vm.c` 中。
