# 启动链后续工作

当前 EFI 启动链已经能把控制权交给 kernel ELF，并让内核打开自己的第一张 Sv39 页表。
下一步要把“能启动”继续推进成“内核真正接管机器”。

## 地址空间

第一张页表只是恒等映射。后续需要建立更正式的内核地址空间：

```text
kernel text      : R/X
kernel rodata    : R
kernel data/bss  : R/W
direct map       : 映射可管理物理内存
MMIO             : 按设备范围映射
```

权限拆分之后，内核才能更早发现写代码段、执行数据段这类错误。

## 设备发现

EFI loader 可以把 DTB 地址传进 `boot_info`，但设备树内容应该由内核解析。当前阶段
先只传地址和大小，后续需要决定用 C 库还是 Rust crate 解析 DTB，并把串口、PLIC、
timer 等设备信息整理成内核自己的设备表。

## trap 和中断

打开页表之后，需要尽快接入 trap：

```text
异常入口
系统调用入口
时钟中断
外部中断
缺页异常
```

这一步会把启动阶段的 SBI 输出、物理页分配器和页表管理继续连接到真正的内核控制流。

## 多 hart

EFI app 通常只在 boot hart 上运行。内核需要根据 `boot_info->boot_hart_id` 确定主
hart，再通过 SBI HSM 等机制启动其它 hart。其它 hart 进入内核后应该走单独的 secondary
entry，不能重复执行全局初始化。
