# Kernel Entry

EFI loader 跳进 kernel ELF 后，内核第一个 C 入口是：

```c
void kernel_entry(struct kernel_boot_info *boot_info)
```

这个入口不是最终 `main()`。它只负责一件事：把 EFI loader 交来的启动材料接住，并
推进到“内核已经有自己的物理页分配器和第一张 Sv39 页表”。

## 调用链

```text
jump_to_kernel()
  -> kernel_entry(boot_info)
     -> validate_boot_info()：校验启动 ABI 和必要字段。
     -> print_boot_info()：打印 loader 交来的启动信息。
     -> memory_probe()：整理 EFI memory map。
     -> page_allocator_init()：初始化物理页分配器。
     -> early_vm_enable()：建立并启用第一张 Sv39 页表。
     -> early_halt_forever()：当前阶段停在这里。
```

`jump_to_kernel()` 的职责在 [02 EFI Loader](02-uefi-loader.md) 里讲过：切栈、把
`boot_info` 放进 `a0`、清 `satp`，然后跳到 kernel ELF 入口。到这里之后，EFI
Boot Services 已经不可用了。

## validate_boot_info()

`validate_boot_info()` 是入口的第一道防线。它检查：

- `boot_info` 指针非空。
- `magic`、`version`、`size` 符合当前 ABI。
- 必要 `flags` 都存在。
- memory map、kernel image、kernel stack 等必要范围非空。

`boot_info` 字段含义见 [03 Boot Info](03-boot-info.md)。这里不重复展开。内核必须
先校验它，因为后面的 memory map 解析和页表建立都会把这些地址当成可信输入。

## print_boot_info()

`print_boot_info()` 通过 `early_log` 打印启动信息。当前阶段还没有 UART 驱动和正式
`printk`，所以底层仍然是 SBI console。

这一步的价值是调试：如果上板时启动失败，日志能直接看到 boot hart、DTB、最终
memory map、kernel image、`boot_info` 和初始栈的地址范围。

## memory_probe()

`memory_probe()` 消费 `boot_info` 里的最终 EFI memory map，并整理两类范围：

```text
usable ranges   : 可以交给物理页分配器的 ConventionalMemory
reserved ranges : kernel image、boot_info、最终 memory map、初始内核栈
```

细节见 [05 Memory Map 与物理页分配](05-memory-map-and-pages.md)。这里要记住的是：
内核不能直接把整张 EFI memory map 里的内存都当空闲页用，必须先扣掉启动时已经占用
的材料。

## page_allocator_init()

`page_allocator_init()` 接管 `memory_probe()` 整理出的 usable ranges。当前实现会
从可用内存中预留 bitmap/refcount 元数据，然后用它管理 4KB 物理页。

这一步完成后，后续页表页、`kmalloc` 后端和第一版用户页都可以通过
`phys_alloc_pages()` 申请。接口和元数据设计见 [07 内存接口](07-memory-interfaces.md)。

## early_vm_enable()

`early_vm_enable()` 建立第一张内核 Sv39 页表。它选择启动后必须立刻可访问的范围，
真正页表操作则交给 `vm.c`：

```text
kernel sections
initial kernel stack
boot_info
final EFI memory map
DTB
usable physical ranges
```

当前采用恒等映射：

```text
virtual address == physical address
```

这样写入 `satp` 后，正在执行的代码、当前栈、启动信息和物理页分配器新分配的页仍
能继续访问。页表权限、A/D 位和页表页按需分配见
[06 第一张 Sv39 页表](06-first-page-table.md)。

## 当前停机点

`kernel_entry()` 当前在 `early_vm_enable()` 后停在 `early_halt_forever()`。这表示
本阶段已经完成启动 ABI、memory map、物理页分配器和第一张页表的接管。

后面要接的内容不写在这个入口文档里展开：

- DTB 解析和 `platform_info`。
- UART driver 和正式 `printk`。
- trap / timer / irq 初始化。
- `kmalloc`。
- 用户地址空间。
