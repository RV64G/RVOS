# EFI Loader

EFI loader 是启动链里的过渡层。它仍然运行在固件提供的 UEFI 环境里，可以调用
Boot Services、文件协议和 console 输出；但它不是长期内核的一部分。它的目标是把
内核运行所需的材料准备好，然后退出 Boot Services，把控制权交给 `kernel_entry()`。

这一页按 `efi_main()` 的调用链阅读。函数名后的一句话说明“做什么”；具体为什么要
这样做，放在每个阶段的说明里。

## 调用链

```text
efi_main()
  -> efi_allocate_pages()：为 boot_info 和初始内核栈申请页。
  -> efi_collect_boot_info()：收集 DTB、boot hart id 和一版 memory map。
     -> find_dtb()：从 EFI configuration table 找 DTB。
     -> get_boot_hart_id()：通过 RISC-V EFI Boot Protocol 获取 boot hart。
     -> efi_get_memory_map()：读取当前 EFI memory map。
  -> efi_print_boot_info()：打印已经收集到的启动信息。
  -> efi_print_memory_map()：打印当前 memory map 摘要。
  -> efi_free_memory_map()：释放调试用 memory map。
  -> efi_load_kernel_elf()：从 ESP 打开并装载 `RVOS/KERNEL.ELF`。
     -> open_kernel_file()：定位内核 ELF 文件。
        -> open_kernel_path()：按 `RVOS/KERNEL.ELF` 逐级打开路径。
     -> read_whole_file()：把 ELF 文件读入 EFI pool 临时缓冲区。
     -> validate_kernel_elf()：检查 ELF header 和所有 `PT_LOAD` 段。
     -> allocate_kernel_range()：为内核装载范围申请物理页。
     -> load_segments()：复制 `PT_LOAD` 内容并清零 bss 区域。
  -> efi_get_memory_map()：重新读取最终 memory map。
  -> ExitBootServices()：结束 EFI Boot Services。
  -> jump_to_kernel()：切换栈、清 `satp`，跳转到 kernel ELF 入口。
  -> kernel_entry()：内核接管 `boot_info`、memory map 和页表。
```

## EFI 环境

`efi_main(EFI_HANDLE image_handle, efi_system_table_t *system_table)` 的两个参数来自
UEFI ABI。

`image_handle` 表示当前这个已经被固件加载的 EFI 应用。loader 后续通过它查询
Loaded Image Protocol，从而找到当前启动介质。

`system_table` 保存 UEFI 服务表。EFI 应用没有 libc，也没有 RVOS 内核服务；它调用
`system_table->boot_services` 和 `system_table->con_out` 里的函数指针，实际执行者
是 U-Boot 或 EDK2。

在开发板上，链路通常是：

```text
OpenSBI -> U-Boot -> bootefi -> RVOS EFI app
```

在 QEMU 上，链路通常是：

```text
OpenSBI -> EDK2 -> Boot Manager -> RVOS EFI app
```

UEFI 规范让同一个 EFI app 可以跑在这两条链路上，但实现细节仍可能不同。因此
loader 只依赖必要协议：configuration table、RISC-V boot protocol、loaded image、
simple file system、file protocol 和 memory map。

## 启动信息

`efi_collect_boot_info()` 填充 `struct kernel_boot_info`。这个结构是 EFI loader 和
kernel ELF 之间的启动 ABI。内核不会再调用 EFI API，只读取这个结构。

当前会传给内核的材料包括：

- DTB 物理地址和大小。
- EFI memory map 的物理地址、字节数、descriptor 大小和版本。
- boot hart id。
- kernel ELF 的物理装载范围。
- `boot_info` 自己所在页。
- 初始内核栈所在页。

DTB 通过 EFI configuration table 查找。RISC-V boot hart id 通过 RISC-V EFI Boot
Protocol 获取。memory map 在启动中会读取两次：第一次用于调试打印，第二次是
`ExitBootServices()` 前的最终版本。

## 文件加载

EFI loader 不解析 FAT32。它通过 UEFI 文件协议访问当前启动介质：

```text
image_handle
  -> Loaded Image Protocol
  -> loaded_image->device_handle
  -> Simple File System Protocol
  -> open_volume()
  -> RVOS/KERNEL.ELF
```

`open_kernel_path()` 按目录逐级打开 `RVOS/KERNEL.ELF`。这里故意不做多路径 fallback。
启动介质布局如果不满足约定，就应该直接失败；否则很容易误读另一份内核。

`read_whole_file()` 会把整个 ELF 读进 EFI pool 临时缓冲区。这个缓冲区只属于 loader
阶段，ELF 装载完成后释放。当前内核 ELF 很小，一次性读取能让校验逻辑更直接；后面
如果镜像变大，可以改成按段读取。

## ELF 装载

loader 按 Program Header 装载内核，而不是按 Section Header。运行时真正需要进入
内存的是 `PT_LOAD` 段。

`validate_kernel_elf()` 会检查：

- ELF magic。
- 64-bit little-endian。
- executable 类型。
- RISC-V machine。
- program header 表是否在文件范围内。
- 每个 `PT_LOAD` 的文件范围和内存范围是否合理。

它还会扫描所有 `PT_LOAD`，计算一个页对齐的总装载范围。这里不能“每个段申请一次
页”，因为两个段可能落在同一页内。逐段申请会导致同一物理页被重复申请或互相覆盖。
先计算总范围、再统一申请，可以保证整片内核物理地址归 loader/内核所有。

`load_segments()` 再按 program header 复制内容：

```text
p_filesz 字节：从 ELF 文件复制到 p_paddr
p_memsz - p_filesz：清零，用作 bss 等内存段
```

装载完成后，`efi_load_kernel_elf()` 返回三项信息：

- `entry`：ELF header 里的 `e_entry`。
- `load_start`：所有 `PT_LOAD` 覆盖到的最小物理地址。
- `load_size`：页对齐后的内核装载大小。

这些会写进 `boot_info`，内核用它们把 kernel image 从可分配页中排除。

## memory map 和 map_key

`GetMemoryMap()` 返回当前物理内存布局。每条 descriptor 说明一段物理内存的类型、
起始地址、页数和属性。

`GetMemoryMap()` 同时返回 `map_key`。这个 key 可以理解成当前 memory map 的版本
凭证。只要还在调用 `AllocatePool`、`AllocatePages`、`FreePool` 或 `FreePages`，
固件内部内存表就可能变化，旧 key 就可能失效。

`ExitBootServices(image_handle, map_key)` 要求传入最后一次成功读取 memory map 时
得到的 key。如果 key 过期，固件会拒绝退出 Boot Services。

因此正式交接顺序必须是：

```text
完成所有必须保留的分配
释放所有临时 buffer
最后一次 GetMemoryMap，保存最终 memory map 和 map_key
立刻 ExitBootServices
跳进 RVOS 内核入口
```

第一份 memory map 只用于调试打印，打印后会释放。释放本身会改变 memory map。
第二份 memory map 是交接前重新读取的最终版本，不能释放，要通过 `boot_info` 留给
内核。

## 退出 Boot Services

`ExitBootServices()` 之后，所有 Boot Services 都失效。内核不能再调用 EFI 的文件、
内存分配或 console 输出接口。

成功退出后，loader 调用 `jump_to_kernel(entry, stack_top, boot_info)`。这个函数在
RISC-V 汇编里完成三件事：

- 把 `sp` 切到 EFI loader 为内核申请的初始栈顶。
- 把 `boot_info` 放到 `a0`，作为 `kernel_entry()` 的第一个参数。
- 清 `satp` 并执行 `sfence.vma`，回到物理地址直通环境。

最后用 `jr` 跳到 ELF entry。这里不是 `call`，因为内核成功启动后不应该返回 EFI
loader。

## 设计边界

EFI loader 负责“交接材料”，不负责长期内核能力：

- 可以用 EFI 文件协议读取 kernel ELF，但内核文件系统要自己实现。
- 可以用 EFI `AllocatePages()` 准备内核镜像、栈和 `boot_info`，但退出后内存管理
  由 `boot_memory` / `page_alloc` 接管。
- 可以从 EFI configuration table 找 DTB，但 DTB 内容由内核解析。
- 可以打印 EFI 阶段日志，但内核早期输出走 SBI，正式输出后面由 UART/console 接管。

这样边界清楚：固件提供启动服务，EFI loader 完成交接，kernel ELF 负责长期运行。
