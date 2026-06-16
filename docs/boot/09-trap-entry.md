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

trap_handle(frame) -> trap_result
  -> handle_interrupt(frame, code)：处理 software/timer/external interrupt。
  -> handle_exception(frame, code)：处理 breakpoint、ecall、page fault 等同步异常。
  -> 根据处理结果决定返回 trap 前现场，或打印现场并停机。
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

`trap_vector` 和 `struct trap_frame` 共用 `arch/riscv/trap_frame_offsets.h` 里的偏移
常量。C 侧用 `_Static_assert` 校验结构体字段偏移，避免汇编里保存的是一个布局、C
代码按另一个布局解释。

## 可返回同步异常

当前 breakpoint 只作为调试异常处理。若代码发出固定的 32-bit `ebreak`：

```asm
.word 0x00100073
```

触发后 `scause=3`，表示 breakpoint exception。`trap_handle()` 识别这个异常后把
`frame->sepc += 4`，让 `sret` 回到 `ebreak` 后一条指令。

这里不用汇编助记符 `ebreak`，是为了避免开启 RVC 时 assembler 选择 16-bit
`c.ebreak`。如果实际指令长度是 2 字节，却固定 `sepc += 4`，返回地址就错了。

这个自检只在 `KERNEL_SELFTEST` 构建里运行。默认启动只安装 trap 入口，避免每次启动
都制造一次调试异常，影响后续 timer/tick 日志。

`ecall` 暂时不放进这条自检。S-mode `ecall` 在当前启动环境里用于 SBI 调用，可能直接
进入 OpenSBI/M-mode，而不是进入我们的 S-mode trap handler。真正要验证 syscall 路径，
应该等用户态出现后，用 U-mode `ecall` 测试。

## 当前分发范围

当前已经建立分发骨架。`trap_handle()` 返回 `enum trap_result`；
`handle_interrupt()` / `handle_exception()` 不直接决定停机，而是返回处理结果：

```text
TRAP_HANDLED : trap 已处理，可以恢复现场并 sret。
TRAP_FATAL   : 当前还没有处理能力，统一打印 trap_frame 后停机。
```

这样后续 page fault、ecall、external interrupt 可以逐步从 `TRAP_FATAL` 改成
`TRAP_HANDLED`，不用把停机策略散落在每个分支里。

`trap_vector` 当前不根据返回值做汇编分支：fatal trap 会在 C 侧停机，只有 handled
trap 才会回到汇编恢复现场。保留返回值是为了让 C 侧接口语义明确，也方便后续合成
trap 测试或调度返回路径复用。

当前大部分处理仍然是 fatal：

- software interrupt：预留给 IPI/调度唤醒；
- timer interrupt：预留给 SBI timer 和系统 tick；
- external interrupt：预留给 PLIC/AIA 和 UART 输入；
- illegal instruction：打印现场后停机；
- page fault：打印现场后停机；
- ecall：当前还没有 syscall 框架，打印现场后停机；
- breakpoint：前进 `sepc` 后返回。

后续接入 timer 时，会先让 timer interrupt 从“打印并停机”变成“增加 tick、设置下一次
timer、返回”。再往后，ecall 会接到 syscall 分发，page fault 会接到虚拟内存处理。
