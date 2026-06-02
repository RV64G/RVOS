# Kernel Entry

EFI loader 跳进 kernel ELF 后，内核的第一个 C 入口是 `kernel_entry`。

从 loader 到内核入口不走普通 C 函数调用，而是走 RISC-V 交接代码：

```text
boot/efi/riscv/jump.S
```

这段代码先把 `sp` 切到 EFI loader 准备的初始内核栈，再把 `boot_info` 放进 `a0`，
最后用 `jr` 跳到 ELF header 里的入口地址。内核不应该返回 EFI loader。

## 入口职责

`kernel_entry` 当前按这个顺序工作：

```text
校验 boot_info
打印启动信息
解析 EFI memory map
建立物理页分配器
建立第一张 Sv39 页表
打开 satp
```

这个入口还不是最终内核主函数。它更像“内核接管固件交接材料”的第一站。串口驱动、
正式 printk、trap 初始化、设备发现、多 hart 启动和调度器都应该在这个边界之后
继续接上。

## 早期输出

当前阶段还没有串口驱动和正式 `printk`，所以内核入口直接通过 SBI debug console
打印日志。只要 OpenSBI 提供对应 SBI 调用，这条输出路径就能在 QEMU 和开发板上
尽早暴露启动状态。

后续接入正式串口驱动后，早期日志函数应该退到启动调试用途，常规输出交给 `printk`。
