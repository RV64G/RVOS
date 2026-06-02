# EFI Loader

EFI loader 是启动链里的过渡层。它运行在固件提供的 UEFI 环境里，负责把内核长期
运行所需的材料准备好，然后退出 Boot Services，把控制权交给 kernel ELF。

## EFI 服务来源

EFI 应用不是调用 RVOS 内核，也不是依赖 libc。`efi_main` 收到的 `system_table`
里保存了一组函数指针，例如 console 输出、memory map 查询、pool 分配和页分配。
EFI 应用调用这些函数指针，实际执行者是当前启动固件。

在开发板上，这些服务来自 U-Boot 的 UEFI implementation：

```text
OpenSBI -> U-Boot -> bootefi -> RVOS EFI app
```

在 QEMU 上，这些服务来自 EDK2：

```text
OpenSBI -> EDK2 -> Boot Manager -> RVOS EFI app
```

UEFI 约定保证同一个 EFI app 可以在这两条链路上运行，但不同实现仍可能有差异。

## 文件加载

EFI loader 读取内核 ELF 时不自己解析 FAT32，而是走 UEFI 文件协议：

```text
Loaded Image Protocol
  -> 找到当前 EFI app 所在的设备
Simple File System Protocol
  -> 打开这个设备上的 ESP 文件系统
File Protocol
  -> 打开并读取 \RVOS\KERNEL.ELF
```

当前 loader 会校验 ELF64/RISC-V 文件头和 program header 表。确认文件存在、格式
正确后，根据 `PT_LOAD` 段计算页对齐的物理装载区间，把段内容复制到 ELF 指定的
`p_paddr`。

这里不能简单地“每个 `PT_LOAD` 段申请一次页”。两个段可能落在同一页内，逐段申请
会互相冲突。先算总装载范围，再统一按地址申请，可以保证这片物理地址归 loader 和
内核所有。

## EFI 阶段分配

EFI 阶段确实需要调用固件分配接口，但它只应该服务于启动器自己。

`AllocatePool` 可以理解成 EFI 阶段的临时 `malloc`。它适合保存路径字符串、临时
文件缓冲区、探测阶段的 memory map buffer 这类小对象。它不强调页对齐，也不适合
承载内核镜像。

`AllocatePages` 可以理解成 EFI 阶段的物理页分配。内核 ELF 的加载区域、初始内核
栈、最终要传给内核的 `boot_info` 和最终 memory map，都更适合按页申请。这样 RVOS
接管后可以直接把这些页标记为已占用。

这些分配必须发生在 `ExitBootServices` 之前。退出之后 Boot Services 全部失效，
内核不能再调用 EFI 的 `AllocatePool` 或 `AllocatePages` 做普通内存管理。

## memory map 和 map_key

`GetMemoryMap` 返回当前物理内存布局。返回结果是一组 descriptor，每条记录描述一段
物理内存的类型、起始地址、页数和属性。

`GetMemoryMap` 同时返回 `map_key`。这个 key 可以理解成当前 memory map 的版本凭证。
只要还在调用 `AllocatePool`、`AllocatePages`、`FreePool` 或 `FreePages`，固件内部
内存表就可能变化，旧 key 就可能失效。

`ExitBootServices(image_handle, map_key)` 要求传入最后一次成功读取 memory map 时
得到的 key。如果 key 过期，固件会拒绝退出 Boot Services。

正式交接顺序应该是：

```text
完成所有必须保留的分配
释放所有临时 buffer
最后一次 GetMemoryMap，保存最终 memory map 和 map_key
立刻 ExitBootServices
跳进 RVOS 内核入口
```

因此 loader 里会有两份 memory map。第一份只用于调试打印，打印完会释放；释放本身
会改变 memory map。第二份是在交接前重新读取的最终 memory map，它不会释放，而是
写进 `boot_info` 留给内核。

## 交出控制权

装载完成后，EFI loader 重新读取最终 memory map，退出 Boot Services，切到初始内核
栈，再跳到 ELF header 里的 `e_entry`。

RISC-V UEFI 固件可能在 S-mode 开着自己的页表。RVOS 的 kernel ELF 当前按物理地址
链接并装载，所以跳转前会清 `satp` 并执行 `sfence.vma`，让后续取指和访存回到物理
地址直通状态。
