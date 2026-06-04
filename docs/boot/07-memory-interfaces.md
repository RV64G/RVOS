# 内存接口

这一页是当前内存接口速查。更完整的流程说明见：

- [05 Memory Map 与物理页分配](05-memory-map-and-pages.md)
- [06 第一张 Sv39 页表](06-first-page-table.md)

## boot_memory

`boot_memory` 消费 `boot_info` 中的最终 EFI memory map，输出启动期内存快照。

常用接口在 `kernel/boot_memory.h`：

```c
const struct boot_memory_state *memory_state(void);
int memory_probe(const struct kernel_boot_info *boot_info);
uint64_t memory_available_pages(void);
```

`memory_probe()` 的输入是 EFI loader 传入的 `boot_info`。它会整理：

- `usable_ranges`：允许交给页分配器的物理范围。
- `reserved_ranges`：启动期必须保留的物理范围。

它不分配页，也不建立页表。

## page_alloc

`page_alloc` 管理 4KB 物理页。常用接口在 `mm/page_alloc.h`：

```c
int page_allocator_init(void);
int page_allocator_ready(void);
uint64_t page_available_pages(void);
void *phys_alloc_pages(uint64_t pages);
void phys_free_pages(void *addr, uint64_t pages);
```

当前实现使用 bitmap/refcount：

- bitmap 记录 PFN 当前是否空闲。
- refcount 当前只使用 0/1，用来拒绝 double free、释放未分配页和越界释放。

`phys_alloc_pages()` 返回物理地址。当前内核保持恒等映射，所以早期代码可以直接把
返回值当指针使用。后面引入非恒等 direct map 后，需要显式 `phys_to_virt()`。

## vm

`vm` 操作 Sv39 页表，建立或解除 VA 到 PA 的映射。常用接口在 `mm/vm.h`：

```c
void vm_space_init(struct vm_space *space);
int vm_space_create(struct vm_space *space);
int vm_map_range(struct vm_space *space, uint64_t va, uint64_t pa, uint64_t size, uint64_t flags);
int vm_identity_map(struct vm_space *space, uint64_t start, uint64_t size, uint64_t flags);
int vm_unmap_range(struct vm_space *space, uint64_t va, uint64_t size);
void vm_activate_sv39(const struct vm_space *space);
```

`vm` 不管理物理页所有权。映射前，调用者必须先确定物理地址来自哪里：

- `phys_alloc_pages()` 分配出的页。
- EFI loader 传入的启动材料。
- DTB 或平台信息提供的 MMIO 地址。

`vm_unmap_range()` 只清 PTE，不释放物理页。释放物理页应该由更高层所有者决定。

## early_vm

`early_vm` 不是另一套页表实现。它只选择启动后必须立刻可访问的范围，然后调用 `vm`
接口建立映射。

常用接口在 `mm/early_vm.h`：

```c
int early_vm_enable(const struct kernel_boot_info *boot_info);
struct vm_space *kernel_vm_space(void);
```

`kernel_vm_space()` 返回当前内核页表对象。后续 MMIO、vmalloc、用户地址空间共享内核
映射等功能都可以基于这张 kernel VM 扩展。
