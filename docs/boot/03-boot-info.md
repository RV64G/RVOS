# Boot Info

`boot_info` 是 EFI loader 和 RVOS kernel ELF 之间的启动 ABI。EFI loader 退出
Boot Services 后，内核不能再调用 EFI API，所以 loader 必须把内核需要的启动材料
提前写进一个普通 C 结构。

RISC-V 调用约定里，函数第一个参数放在 `a0`。`jump_to_kernel()` 会把 `boot_info`
指针放入 `a0`，所以 `kernel_entry(struct kernel_boot_info *boot_info)` 能直接收到
这份结构。

结构定义在 `include/kernel_boot_info.h`。

## 和 EFI Loader 的关系

EFI loader 负责填写 `boot_info`。完整 loader 调用链见
[02 EFI Loader](02-uefi-loader.md)。这一页只关心 `boot_info` 作为 ABI 本身：
哪些字段存在、字段从哪里来、内核为什么需要它们。

内核侧从 `kernel_entry()` 开始消费同一份结构：

```text
kernel_entry(boot_info)
  -> validate_boot_info()：校验 magic/version/size/flags 和必要范围。
  -> memory_probe()：根据 memory map 排除启动保留页。
  -> page_allocator_init()：接管剩余可用物理页。
  -> early_vm_enable()：映射 boot_info、memory map、DTB 和内核范围。
```

## 固定头部

`boot_info` 前四个字段用于校验 ABI：

- `magic`：确认传入指针确实指向 RVOS 启动信息。
- `version`：标记结构版本，给未来扩展留下兼容空间。
- `size`：记录 loader 实际写入的结构大小。
- `flags`：标记哪些后续字段有效。

启动 ABI 不能靠“看起来像”来猜。内核入口必须先验证这些字段，不满足要求就停机，
避免拿错误地址继续解析 memory map 或写页表。

`magic` 当前是 `"RVOSBOOT"` 的 little-endian `uint64_t` 表示。它打印成：

```text
0x544f4f42534f5652
```

内存里的字节顺序是：

```text
52 56 4f 53 42 4f 4f 54  =>  R V O S B O O T
```

## 字段来源

当前 `boot_info` 只放固定字段，不放变长数组。

| 字段 | 来源 | 消费方 |
| --- | --- | --- |
| `dtb_phys` / `dtb_size` | EFI configuration table | 后续 DTB 解析和设备发现 |
| `efi_memory_map_*` | 最终 `GetMemoryMap()` | `memory_probe()` |
| `boot_hart_id` | RISC-V EFI Boot Protocol | 多 hart 初始化 |
| `kernel_phys_base` / `kernel_size` | `efi_load_kernel_elf()` | `memory_probe()` 和早期页表 |
| `boot_info_phys` / `boot_info_size` | EFI `AllocatePages()` | `memory_probe()` 和早期页表 |
| `kernel_stack_phys` / `kernel_stack_size` | EFI `AllocatePages()` | `memory_probe()` 和早期页表 |

这些字段在 EFI 阶段怎样被收集，见 [02 EFI Loader](02-uefi-loader.md) 的启动信息、
ELF 装载和 memory map 小节。

`efi_descriptor_size` 不能省略。UEFI 允许 memory descriptor 扩展，因此内核遍历
memory map 时必须按固件返回的 descriptor size 前进，而不是按当前 C 结构体的
`sizeof` 前进。

## flags

`flags` 是有效字段集合。loader 只有在某类信息成功获得后才置位，例如：

```text
KERNEL_BOOT_HAS_DTB
KERNEL_BOOT_HAS_EFI_MEMORY_MAP
KERNEL_BOOT_HAS_BOOT_HART_ID
KERNEL_BOOT_HAS_KERNEL_IMAGE
KERNEL_BOOT_HAS_KERNEL_STACK
```

内核当前把下面四类视为硬要求：

- EFI memory map。
- boot hart id。
- kernel image 范围。
- 初始 kernel stack。

DTB 暂时不是硬要求，因为最早期内存接管不依赖设备树。等 UART、timer、PLIC/AIA
初始化开始依赖 DTB 后，可以把它提升为必要条件。

## boot hart id

EFI app 通常只在 boot hart 上运行。机器可以有多个 hart，但固件会选择一个 hart
进入下一阶段，其他 hart 不会同时跑 `efi_main()`。

QEMU 日志里可能出现：

```text
Platform HART Count : 4
Boot HART ID        : 1
```

这表示机器有 4 个 hart，当前启动流程跑在 hart 1 上。boot hart 不保证是 0。

RISC-V boot hart id 通过 `RISCV_EFI_BOOT_PROTOCOL.GetBootHartId()` 获取。这里不从
DTB 猜，也不默认写 0；如果固件不提供规范协议，就让启动阶段明确失败。

## 生命周期

`boot_info` 自身由 EFI loader 用 `AllocatePages()` 申请。它不是临时 pool buffer，
退出 Boot Services 后仍要交给内核读取。

内核接手后，`memory_probe()` 会把这些启动材料从可分配页中排除：

- kernel ELF 装载范围。
- `boot_info` 所在页。
- 最终 EFI memory map 所在页。
- 初始内核栈。

随后 `early_vm_enable()` 会把这些范围映射进第一张 Sv39 页表。这样打开 `satp` 后，
内核仍能继续读取 `boot_info`、memory map 和 DTB。

## 扩展原则

`boot_info` 是 ABI，不是普通内部结构。扩展时应该遵守：

- 新字段放在结构末尾。
- 增加对应 `KERNEL_BOOT_HAS_*` flag。
- 保留 `version` 和 `size` 校验。
- 不在结构里放变长数组；大对象传物理地址和大小。
- loader 填字段，kernel 校验字段，双方职责分开。
