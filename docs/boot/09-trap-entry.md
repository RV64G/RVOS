# Trap 入口与返回

UART/`printk` 接管后，内核可以开始安装 S-mode trap 入口。第一版 trap 目标是建立
统一入口和分发骨架：所有异常和中断先保存成 `trap_frame`，再由 C 代码根据
`scause` 分类处理。

## 调用链

```text
kernel_entry()
  -> trap_init()：把 trap_vector 写入 stvec。

trap_vector
  -> 保存通用寄存器和 sepc/sstatus/scause/stval 到 trap_frame。
  -> trap_handle(frame)：按 scause 分发到 interrupt/exception 处理路径。
  -> 恢复 sepc/sstatus 和通用寄存器。
  -> sret：返回 trap 前后的控制流。

trap_handle(frame)
  -> handle_interrupt(frame, code)：处理 software/timer/external interrupt。
  -> handle_exception(frame, code)：处理 breakpoint、ecall、page fault 等同步异常。
```

## stvec

`trap_init()` 使用 Direct 模式：

```text
stvec = trap_vector
```

Direct 模式下，所有异常和中断都会先进入同一个入口。后续可以在 C 侧根据 `scause`
分发到 page fault、ecall、timer interrupt、external interrupt 等处理函数。

## trap_frame

RISC-V 进入 trap 时不会自动保存通用寄存器，也不会自动切换栈。`trap_vector` 当前沿用
正在使用的内核栈，手动压入：

- `x1..x31` 中需要恢复的通用寄存器；
- `sepc`：trap 返回地址；
- `sstatus`：S-mode 状态；
- `scause`：trap 原因；
- `stval`：异常附加值。

这些字段组成 `struct trap_frame`，C 侧只通过这个结构观察和修改 trap 状态。

## breakpoint 为什么能返回

当前 breakpoint 只作为调试异常处理。若代码发出固定的 32-bit `ebreak`：

```asm
.word 0x00100073
```

触发后 `scause=3`，表示 breakpoint exception。`trap_handle()` 识别这个异常后把
`frame->sepc += 4`，让 `sret` 回到 `ebreak` 后一条指令。

这里不用汇编助记符 `ebreak`，是为了避免开启 RVC 时 assembler 选择 16-bit
`c.ebreak`。如果实际指令长度是 2 字节，却固定 `sepc += 4`，返回地址就错了。

这个自检不再放在默认启动主线里。默认启动只安装 trap 入口，避免每次启动都制造一
次调试异常，影响后续 timer/tick 日志。

## 当前分发范围

当前已经建立分发骨架，但大部分处理仍然是打印并停机：

- software interrupt：预留给 IPI/调度唤醒；
- timer interrupt：预留给 SBI timer 和系统 tick；
- external interrupt：预留给 PLIC/AIA 和 UART 输入；
- illegal instruction：打印现场后停机；
- page fault：打印现场后停机；
- ecall：当前还没有 syscall 框架，打印现场后停机；
- breakpoint：前进 `sepc` 后返回。

后续接入 timer 时，会先让 timer interrupt 从“打印并停机”变成“增加 tick、设置下一次
timer、返回”。再往后，ecall 会接到 syscall 分发，page fault 会接到虚拟内存处理。
