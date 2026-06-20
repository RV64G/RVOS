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
uint32_t phys_page_state(void *addr);
void *phys_alloc_pages(uint64_t pages);
void phys_free_pages(void *addr, uint64_t pages);
```

当前实现使用 bitmap/refcount：

- bitmap 记录 PFN 当前是否空闲。
- refcount 当前只使用 0/1，用来拒绝 double free、释放未分配页和越界释放。

分配连续页时使用 next-fit 扫描：分配器会记住上次成功分配后的索引，下次从这个位置
继续找，扫到末尾再回到开头。这样连续申请大量单页时，不会每次都从 bitmap 头部重新
跳过已经分配的页。它仍然是简单线性扫描，不是 buddy；如果后面 fork、文件缓存或用户页
带来更高压力，可以在不改外部接口的前提下替换内部算法。

`phys_alloc_pages()` 返回物理地址。当前内核保持恒等映射，所以早期代码可以直接把
返回值当指针使用。后面引入非恒等 direct map 后，需要显式 `phys_to_virt()`。

`phys_page_state()` 只查询一页当前状态，不改变分配器。它主要给调试、自检和后续
地址空间管理使用：

```text
PHYS_PAGE_INVALID   : 页分配器未就绪、地址未对齐或超出管理范围
PHYS_PAGE_FREE      : 当前可分配
PHYS_PAGE_ALLOCATED : 当前已分配，metadata 页也属于这一类
PHYS_PAGE_RESERVED  : 位于 PFN 覆盖范围内，但不是可分配页，通常是 memory map 洞
```

## vm

`vm` 操作 Sv39 页表，建立或解除 VA 到 PA 的映射。常用接口在 `mm/vm.h`：

```c
void vm_space_init(struct vm_space *space);
int vm_space_create(struct vm_space *space);
int vm_copy_kernel_mappings(struct vm_space *dst, const struct vm_space *src);
int vm_map_range(struct vm_space *space, uint64_t va, uint64_t pa, uint64_t size, uint64_t flags);
int vm_identity_map(struct vm_space *space, uint64_t start, uint64_t size, uint64_t flags);
int vm_unmap_range(struct vm_space *space, uint64_t va, uint64_t size);
int vm_query(const struct vm_space *space, uint64_t va, struct vm_mapping *mapping);
void vm_activate_sv39(const struct vm_space *space);
```

`vm` 不管理物理页所有权。映射前，调用者必须先确定物理地址来自哪里：

- `phys_alloc_pages()` 分配出的页。
- EFI loader 传入的启动材料。
- DTB 或平台信息提供的 MMIO 地址。

`VM_MAP_WRITE` 必须和 `VM_MAP_READ` 一起使用。RISC-V Sv39 把 `W=1,R=0` 的叶子 PTE
定义为保留组合，`vm_map_range()` 会直接拒绝这种权限。

`vm_map_range()` 的对外语义是：成功时整段范围都完成映射，失败时不留下本次调用已经
写入的叶子 PTE。当前实现选择“失败后回滚已经写入的 leaf 映射”，而不是复制一份新页表、
成功后再整体切换。后者可以做到更完整的事务语义，但需要处理中间页表页引用计数、旧页表
回收、`satp` 切换和多核 TLB shootdown；现在内核还处在单核早期阶段，成本比收益高。

TODO：后续用户地址空间稳定后，可以把页表修改升级成“路径复制”或“事务日志”形式。到
那时失败路径不仅要撤销 leaf PTE，也应该把本次新建但未使用的中间页表页归还给页分配器。

`vm_unmap_range()` 只清 PTE，不释放物理页。释放物理页应该由更高层所有者决定。

`vm_copy_kernel_mappings()` 用于创建用户页表骨架。它遍历已有内核页表，把 S-mode
叶子映射复制到新页表中，但跳过带 `VM_MAP_USER` 的映射。这样用户页表在 trap 后仍能
执行内核代码、访问 MMIO 和 direct map，同时不会继承其它用户映射。

`vm_query()` 只读页表，用来查询一个虚拟地址是否已经映射。成功时返回：

- `mapping.pa`：输入 VA 对应的物理地址，包含页内偏移。
- `mapping.leaf_size`：覆盖这个 VA 的叶子 PTE 大小，可能是 4KB、2MB 或 1GB。
- `mapping.flags`：`VM_MAP_READ/WRITE/EXEC` 权限组合。

这个接口不会分配页表页，也不会刷新 TLB。后续测试、page fault 调试和用户地址空间
管理可以用它确认页表状态。

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

## kmalloc

`kmalloc` 管理内核小对象分配。常用接口在 `mm/kmalloc.h`：

```c
void kmalloc_init(void);
void *kmalloc(uint64_t size);
void *kzalloc(uint64_t size);
void kfree(void *ptr);
```

它建立在 `phys_alloc_pages()` 之上：空闲链表没有足够空间时，会向页分配器申请更多
4KB 页，再切成小块给调用者。

- `kmalloc(size)` 返回未清零的内核内存。
- `kzalloc(size)` 返回已经清零的内核内存，适合分配 PCB、timer 节点、文件系统节点
  这类结构体。
- `kfree(ptr)` 把内存块插回空闲链表，并尝试和相邻空闲块合并。

当前版本还没有锁，只适合 boot hart 和早期单核运行。等调度和多核接入后，需要在
`kmalloc` / `kfree` 的临界区加自旋锁。

### Header 和 units

第一版 `kmalloc` 采用 K&R 风格空闲链表。每个被管理的块前面都有一个 `Header`：

```text
[ Header ][ 用户可用内存 ... ]
```

`kmalloc()` 返回的是 Header 后面的用户区。`kfree(ptr)` 收到用户区地址后，通过
`(Header *)ptr - 1` 找回块头。

`Header::units` 不是字节数，而是这个块包含多少个 `Header` 单位。假设
`sizeof(Header) == 16`：

```text
units = 1  -> 16 字节，只够 Header
units = 2  -> 32 字节，Header + 16 字节用户区
units = 4  -> 64 字节，Header + 48 字节用户区
```

当前 `Header` 是普通结构体，包含一个空闲链表指针和一个 `uint64_t units`。在 RV64
上它天然按 8 字节对齐；如果以后需要更严格的对齐，再扩展专门的 aligned 分配接口。

这样做的好处是指针运算直接以块单位推进：

```c
block + block->units
```

正好指向这个块的末尾。`kfree()` 合并相邻空闲块时，就可以用这个表达式判断两个块
在内存里是否紧挨着。

### 大块和页归还

如果 `kmalloc(size)` 需要超过 4KB，它会通过 `phys_alloc_pages()` 申请足够多的连续
页，但这些页仍会先进入 `kmalloc` 的空闲链表，再从链表里切出调用者需要的块。

当前 `kfree()` 不会把完整空页还给 `phys_free_pages()`。页一旦交给 `kmalloc`，就留在
内核堆内部复用。这样实现简单，但意味着页分配器看不到这些已经归还给 `kmalloc` 的
空闲空间。后续如果出现长期运行内存压力，再补“整页归还”或 slab/size-class 分配器。

因此 `make test` 里的 `kmalloc` 耗尽测试必须放在内存自检最后。它会让 `kmalloc` 一直
扩堆直到无法再从页分配器拿页，然后验证失败返回和 `kfree` 链表路径；测试结束后不要求
`page_available_pages()` 回到耗尽前。

传统 K&R malloc 会把“向系统申请更多堆内存”的函数叫 `morecore()`。这里没有沿用这个
名字，因为 `core` 是早期“主存”的说法，容易被误解成 CPU core。当前实现里这个步骤
叫 `grow_heap()`：它从 `phys_alloc_pages()` 拿页，并把新页加入 `kmalloc` 空闲链表。

### 为什么从尾部切块

当一个空闲块比请求更大时，`kmalloc` 从空闲块尾部切出已分配块：

```text
[ 原空闲块........................ ]
[ 缩小后的空闲块...... ][ 分配块... ]
```

这样原空闲块仍然留在空闲链表原位置，只需要减少它的 `units`，不用改前后链表指针。
如果从头部切，就要把剩余空闲块移动成新的链表节点，链表更新会多一些。
