# IRQ 与串口输入

这一页说明当前最小中断输入链路。它的目标不是实现完整 tty，而是先证明外部中断、
PLIC、UART RX 和 console 输入缓冲区可以跑通。

## 调用链

```text
kernel_entry()
  -> dtb_init()：读取 PLIC MMIO range、UART MMIO range 和 UART interrupt source id。
  -> platform_map_mmio()：把 UART/PLIC MMIO 映射进当前内核页表。
  -> printk_init()：初始化 UART 输出。
  -> console_init()：初始化 console 输入 ring buffer。
  -> trap_init()：安装统一 trap 入口。
  -> timer_init()：打开 S-mode timer interrupt。
  -> irq_init()：初始化 PLIC，打开 UART RX interrupt。
  -> console_debug_loop()：临时 drain 输入并回显。
```

输入到达后的路径是：

```text
UART RX interrupt
  -> PLIC pending
  -> supervisor external interrupt
  -> trap_vector()
  -> trap_handle()
  -> irq_handle_external()
     -> plic_claim()
     -> uart_handle_interrupt()
        -> console_input_putc()
     -> plic_complete()
  -> console_debug_loop()
     -> console_drain_input()
```

## PLIC 里的信息来自哪里

DTB 负责告诉内核两类“板级事实”：

```text
PLIC base/size      : PLIC MMIO 窗口在物理地址空间的位置
UART interrupt id   : UART 对应的 PLIC interrupt source id
PLIC context        : 当前 boot hart 的 S-mode external interrupt context
```

PLIC 窗口内部的寄存器偏移不是 DTB 数据，而是 PLIC 兼容布局的一部分。代码会访问
这些区域：

```text
priority array       : 每个中断源的优先级数组
enable bitset        : 每个 context 的中断源 enable 位图
threshold register   : 当前 context 的优先级阈值
claim/complete       : 当前 context 的取中断和完成中断寄存器
```

`priority[source] = 1` 表示这个中断源可被投递。`enable[context]` 决定当前 hart/context
是否接收某个 source。`threshold[context] = 0` 表示优先级大于 0 的中断都能通过。

`claim` 和 `complete` 是一对操作：读取 claim 得到当前待处理 IRQ；处理完设备后，把
同一个 IRQ 写回 complete，告诉 PLIC 这次中断已经服务完成。

## PLIC 寄存器布局怎么想象

PLIC 可以看成一块从 `plic_base` 开始的 MMIO 窗口。窗口内部不是 C 结构体，而是几片
寄存器数组：

```text
plic_base
  |
  +-- priority array
  |     priority[0]      32-bit
  |     priority[1]      32-bit
  |     ...
  |     priority[source] 32-bit
  |
  +-- enable bitsets
  |     context 0 enable word 0  sources 0..31
  |     context 0 enable word 1  sources 32..63
  |     ...
  |     context 1 enable word 0  sources 0..31
  |     ...
  |
  +-- context registers
        context 0 threshold
        context 0 claim/complete
        context 1 threshold
        context 1 claim/complete
        ...
```

`priority` 区是数组。每个 interrupt source 占一个 32-bit 寄存器，所以 UART 的优先级
寄存器位置可以这样计算：

```text
priority_offset = priority array 起点 + uart_irq * sizeof(uint32_t)
```

`uart_irq` 是 UART 的 interrupt source id。这个编号来自 DTB，表示“UART 接到了 PLIC
的第几个输入源”。写 `priority[uart_irq] = 1` 后，只是说明这个 source 有非零优先级，
可以参与投递。

`enable` 区不是数组，而是位图。一个 32-bit word 管 32 个 source：

```text
word index = irq / 32
bit index  = irq % 32
```

所以 `plic_enable_source(uart_irq)` 会先找到当前 context 的 enable 区，再把 UART
source 对应的 bit 置 1。优先级非零和 enable bit 打开必须同时满足，中断才会投递给
这个 context。

`context registers` 区按 context 排列。每个 context 至少有两个关键寄存器：

```text
threshold      : 优先级阈值，低于或等于阈值的中断不会投递
claim/complete : 读是 claim，写是 complete
```

`claim/complete` 共用同一个 offset，但读写语义不同：

```text
读 claim     -> 返回当前 context 下要处理的 interrupt source id
写 complete  -> 告诉 PLIC 这个 source 的本次中断已经处理完
```

所以 `irq_handle_external()` 先 claim 得到一个 source id，再按 source id 分发到
UART 或其它设备，最后 complete 同一个 source id。

## PLIC context 从哪里来

有些资料会把 PLIC context 简化成：

```text
hart0 M-mode, hart0 S-mode, hart1 M-mode, hart1 S-mode, ...
```

然后推导出 `2 * hart_id + 1`。这个公式不可靠。真实平台应该读取 PLIC 节点的
`interrupts-extended`：

```text
interrupts-extended = <cpu-intc-phandle interrupt-id> ...
```

每一对 cell 对应一个 PLIC context，pair 在列表里的序号就是 context number。RISC-V
cpu-intc 中：

```text
interrupt-id 11 : M-mode external interrupt
interrupt-id 9  : S-mode external interrupt
```

因此内核的做法是：

```text
1. 根据 boot_hart_id 找到 /cpus/cpu@N/interrupt-controller 的 phandle。
2. 遍历 PLIC interrupts-extended。
3. 找到 phandle 相同且 interrupt-id == 9 的 pair。
4. 这个 pair 的序号就是当前 boot hart 的 S-mode PLIC context。
```

这样 QEMU、VisionFive V2、Milk-V Mars 或其它 RISC-V 板子只要 DTB 正确，就不需要在
代码里写死 context 排列。

## irq_init() 为什么需要这些数据

`irq_init()` 是外部中断输入链路的装配点。它不扫描硬件，也不猜地址，只消费前面
`dtb_init()` 已经整理进 `platform_info` 的平台信息。

```text
const struct platform_info *platform = platform_info();
```

`platform` 是 DTB 解析结果的只读视图。IRQ 初始化需要从里面取：

```text
irq_base / irq_size      : PLIC MMIO 窗口，用来访问 PLIC 寄存器
uart_irq                 : UART 对应的 PLIC source id
irq_context              : 当前 boot hart 的 S-mode PLIC context
```

这几个值缺一不可。没有 PLIC range，就不知道中断控制器寄存器在哪里；没有 UART
source id，就不知道应该 enable 哪个中断源；没有 irq_context，就不知道应该配置哪个
PLIC context 的 enable/threshold/claim 寄存器。

```text
plic_base = platform->irq_base
plic_size = platform->irq_size
uart_irq = platform->uart_irq
plic_context = platform->irq_context
```

这些全局变量保存的是“当前内核已选择的 IRQ 控制路径”：

```text
plic_base     : PLIC MMIO 窗口的起点，后续 offset 都相对它计算
plic_size     : PLIC MMIO 窗口大小，用来拒绝越界寄存器访问
uart_irq      : 当前 console UART 的 PLIC source id
plic_context  : 当前 boot hart 的 S-mode PLIC context
```

`plic_base` 和 `plic_size` 成对出现。前者负责定位，后者负责限制访问范围。这样即使 DTB
或后续代码给出错误 offset，`plic_read()` / `plic_write()` 也能先拒绝，而不是直接写到
未知 MMIO 地址。

`uart_irq` 不写死，是因为 UART 的中断源编号属于平台布线。QEMU、开发板、不同 SoC
可以把 UART 接到不同 PLIC source；内核应该相信 DTB，而不是把某个平台的编号写进
驱动主逻辑。

`plic_context` 表达的是“这个 hart 的 S-mode 中断入口”。后面做多核时，每个 hart 都
需要从 DTB 找到自己的 context；如果要把 UART 输入迁到另一个 hart，本质上就是给那个
hart 的 context enable UART source，并让对应 hart 安装 trap/栈/调度状态。

```text
priority_offset = priority array + uart_irq
threshold_offset = context area + plic_context
```

`priority_offset` 指向 UART 这个 source 的优先级寄存器。写入非零优先级后，PLIC 才会
认为这个 source 可以被投递。

`threshold_offset` 指向当前 context 的阈值寄存器。阈值用于过滤低优先级中断；当前
第一版把阈值设成最低，让 UART 这种非零优先级中断能通过。

```text
plic_enable_source(uart_irq)
```

优先级非零只表示“这个 source 有资格成为中断”。它还必须在当前 context 的 enable
bitset 里打开，才会投递给当前 hart。这个步骤就是把 UART source id 对应的 bit 置位。

```text
uart_enable_interrupts()
```

PLIC 只负责外部中断控制器这一层。UART 自己也有中断开关。`uart_enable_interrupts()`
先打开 `IER.RDI`，让输入字符能触发外部中断；TX 方向的 `IER.THRI` 不在初始化时直接
打开，而是在 TX ring buffer 非空时按需打开。

```text
csr_set_sie(SIE_SEIE)
```

最后还要打开当前 hart 的 supervisor external interrupt enable 位。否则 PLIC 即使已经
准备好，中断也会被 hart 的 CSR 层挡住。

## irq_handle_external() 做什么

`irq_handle_external()` 是 trap 层遇到 supervisor external interrupt 后调用的分发函数。

```text
for (;;)
  irq = plic_claim()
  if irq == 0:
    return
  dispatch irq
  plic_complete(irq)
```

这里循环 claim，是因为进入一次 external interrupt 时，PLIC 里可能已经有多个 pending
source。只处理一个就返回也能工作，但会产生更多 trap 往返；第一版直接把当前 context
下已经能 claim 到的中断处理完。

`plic_claim()` 返回的是中断源编号，不是字符。它只说明“哪个设备需要服务”。如果这个
编号等于 `uart_irq`，内核才进入 `uart_handle_interrupt()`，从 UART 的接收寄存器读取
字符并放进 console buffer。

`plic_complete(irq)` 必须在设备处理后执行。claim 只是取走 pending IRQ；complete 才是
告诉 PLIC“这个 IRQ 已经服务完了，可以继续接收同源后续中断”。

## UART RX/TX interrupt

`uart_enable_interrupts()` 打开 ns16550 的 `IER.RDI` 位。之后只要接收缓冲区里有字符，
UART 就会向 PLIC 拉起自己的 interrupt source。

RX 方向，`uart_handle_interrupt()` 只在 `LSR.DR` 表示有数据时读取 `RBR`，并把字符
放进 console ring buffer：

```text
while LSR.DR:
  ch = RBR
  console_input_putc(ch)
```

TX 方向，`printk` 先把字符串放进 TX ring buffer，然后 kick 一次 UART：如果
`LSR.THRE` 表示发送保持寄存器当前可写，就从 TX ring buffer 取一个字符写进 `THR`。
写完后重新读 `LSR.THRE`，如果硬件仍然说可写，就继续写；如果硬件不再可写，就停止，
剩下的字符继续留在 TX ring buffer 里。

```text
while TX ring 非空 && LSR.THRE:
  THR = tx_buffer[tail]
  tail++

if TX ring 空:
  关闭 IER.THRI
else:
  保持 IER.THRI 打开
```

这里没有假定 UART FIFO 一定是 16 字节。内核只相信 `LSR.THRE`：硬件每次说可写，内核
才写一个字符；硬件说不可写，内核立刻停下。比如上层一次输出 32 字节：

```text
printk 写 32 字节到 TX ring buffer
uart_tx_drain_locked()
  如果当前 THR 只接收了 1 字节 -> 只写 1 字节
  还有 31 字节留在 TX ring buffer
  打开 IER.THRI
硬件发完前 1 字节后触发 THR-empty interrupt
uart_handle_interrupt()
  再按 LSR.THRE 继续写后面的字节
```

这样做的目的，是把慢速串口的等待时间从 `sys_write/printk` 路径里移走。调用者只负责
把日志交给 ring buffer；真正按波特率慢慢发送的工作，交给 UART 的发送空中断继续推进。

## 当前哪些是临时的

不是整条链路都是临时的。

可以继续演进的基础部分：

- PLIC 初始化和 external interrupt 分发。
- UART RX interrupt。
- UART TX ring buffer 和 THR-empty interrupt。
- console input ring buffer。

临时体验入口：

- `console_debug_loop()`。

它只是为了当前阶段能看见“输入一个字符，内核回显一个字符”。后续有用户态和系统调用
后，这个入口会被 `read(0, ...)`、shell 或正式 console task 替代。到那时输入路径仍然是
UART IRQ -> console buffer，只是消费者从 debug loop 换成用户程序。
