# Boot Info

`boot_info` 是 EFI loader 和 RVOS 内核之间的启动 ABI。它把“固件知道、内核必须
知道”的信息固定成一个 C 结构，入口函数通过 `a0` 接收它的地址。

结构定义在：

```text
include/kernel_boot_info.h
```

## 固定头部

`magic` 用来确认传进来的指针确实指向 RVOS 约定的启动信息。`version` 用来处理
未来结构扩展。`size` 让新旧 loader/kernel 能判断结构体实际长度。`flags` 表示哪些
字段有效。

这几个字段不是为了好看，而是为了让启动 ABI 可以被明确拒绝。内核入口不应该在
启动参数不完整时继续往下跑。

## 当前字段

第一版只放固定字段，不做变长数组：

```text
magic / version / size / flags
DTB 物理地址和大小
EFI memory map 地址、大小、descriptor size、descriptor version
boot hart id
内核镜像地址和大小
boot_info 自己的地址和大小
初始内核栈地址和大小
```

其中 `descriptor size` 不能省略。UEFI 允许 memory descriptor 在未来扩展，遍历
memory map 时必须按固件返回的 descriptor size 前进，而不是按当前 C 结构体的
`sizeof` 前进。

## boot hart id

EFI app 一般只在 boot hart 上运行。机器可以有多个 hart，但固件会选择一个 hart
进入下一阶段，其他 hart 不会同时跑 `efi_main`。

QEMU 日志里的：

```text
Platform HART Count : 4
Boot HART ID        : 1
```

表示机器有 4 个 hart，当前启动流程跑在 hart 1 上。这个值不是固定 0。

RISC-V boot hart id 只通过 `RISCV_EFI_BOOT_PROTOCOL.GetBootHartId()` 获取。这里
不从 DTB 猜，也不默认写 0；如果固件不提供规范协议，就让启动阶段明确失败。

## 初始内核栈

初始内核栈当前为 32KB。它只用于进入内核后的启动初始化。后续线程栈、进程栈和中断
栈仍然应该由 RVOS 自己管理。

`boot_info` 会记录这段栈的物理地址和大小。内核整理 memory map 时必须把这段页视为
已占用，不能放进物理页分配器。
