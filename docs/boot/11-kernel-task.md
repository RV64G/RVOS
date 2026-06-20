# 内核 Task 与上下文切换

当前阶段先实现内核态 task，不直接做用户态进程。目标是证明内核能在不同内核栈之间
保存和恢复执行流，为后续调度器、用户态 trap 返回和进程模型打基础。

## 调用链

```text
context_switch(old, next)
  -> 保存 old 的 ra/sp/s0-s11。
  -> 恢复 next 的 ra/sp/s0-s11。
  -> ret：继续执行 next 上次让出 CPU 的位置。

task_create()
  -> 初始化 task 的 kernel stack。
  -> 把 context.ra 设置为 task_trampoline。
  -> 把 context.sp 设置为 16 字节对齐后的栈顶。

task_system_init()
  -> 把当前内核启动执行流登记为 boot task。
  -> 初始化 scheduler 当前 task 和 run queue。

task_yield()
  -> sched_yield()。
  -> 从 run queue 中取出下一个 READY task。
  -> 更新 RUNNING/READY 状态。
  -> context_switch(old, next)。

task_trampoline()
  -> 调用 task->entry(task->arg)。
  -> entry 返回后把 task 标记为 ZOMBIE。
  -> task_yield() 切回其它 READY task。

task_sleep_ms(ms)
  -> 当前 task 标记为 BLOCKED。
  -> 调度 task 内嵌的 sleep_timer。
  -> sched_yield() 切到其它 READY task。
  -> timer 到期后 task_wake(task)，重新进入 run queue。

timer interrupt
  -> timer 回调调用 sched_request_reschedule()。
  -> trap_handle() 处理完中断后调用 sched_from_trap(frame)。
  -> 返回下一个 task 的 trap_frame。
  -> trap.S 恢复这个 trap_frame 并 sret。
```

## task 和 scheduler 的边界

旧代码里 PCB、运行队列、时间片和 sleep 唤醒都集中在一个调度文件里。当前迁移时先把
边界拆开：

```text
task.c
  -> 初始化 task 对象、内核栈、初始 context、初始 trap_frame、地址空间指针。

sched.c
  -> 维护 run queue。
  -> 选择下一个 READY task。
  -> 提供 yield 路径和 trap 返回路径两个调度入口。
  -> 在切换 task 时，如果地址空间不同，切换 satp。
```

`struct task` 内嵌 `struct list_head run_node`。run queue 不额外分配链表节点，只把
task 自己挂进去。这样后续加入优先级队列、sleep 队列、wait 队列时，都可以继续复用
同一套 `list_head`。

`struct task` 也内嵌一个 `sleep_timer`。这不是说每个 task 永远都在使用 timer，而是把
“这个 task 的睡眠唤醒事件”放进 task 自己的生命周期里。这样 timer interrupt 到期时只
需要拿到 task 指针并调用 `task_wake(task)`，不需要在中断上下文里 `kmalloc()` 一个
临时 timer 节点，也不需要猜这个节点应该由谁释放。

## context 保存什么

`context_switch()` 是普通内核函数调用边界上的切换，不是 trap 返回。按照 RISC-V C ABI，
只需要保存 callee-saved 寄存器：

```text
ra, sp, s0-s11
```

`a0-a7` 和 `t0-t6` 是 caller-saved，普通 C 调用本来就不能指望它们跨函数保持不变。
因此它们不属于 `struct context`。

`arch/riscv/context_offsets.h` 和 `struct context` 共用一套偏移约定，C 侧用
`_Static_assert` 校验结构布局，避免汇编和 C 对同一块内存有不同解释。

## trap 返回路径上的切换

`task_yield()` 解决的是普通函数调用边界上的协作式切换；timer interrupt 带来的抢占式
切换不能直接照搬这条路径。

timer interrupt 发生时，`trap.S` 已经把被打断 task 的完整现场压成 `trap_frame`。这时
如果要切换，最自然的做法不是在 timer 回调里 `context_switch()`，而是：

```text
current->trap_frame = 当前 trap_frame
next = 下一个 READY task
return next->trap_frame 给 trap.S
```

然后由 `trap.S` 恢复 `next->trap_frame` 并 `sret`。这样每个 task 都从自己的中断现场
继续执行，`sstatus/sepc/sp` 也跟着对应 task 一起恢复。

新建 task 还没有被中断过，所以 `task_create()` 会在它的内核栈顶部预先构造一个初始
`trap_frame`：

- `sepc = task_trampoline`；
- `sp = 16 字节对齐后的栈顶`；
- `sstatus` 设置为返回 S-mode，并在 `sret` 后打开中断。

这让一个新 task 第一次也可以通过 trap 返回路径启动。

## task 与地址空间

`struct task` 现在带有一个 `vm_space` 指针。当前阶段所有 task 仍然使用同一张
`kernel_vm_space()`：

```text
boot task      -> kernel_vm_space()
普通内核 task -> 默认继承创建者的 vm_space
```

这一步只是把 PCB 和 VM 的边界接起来，还没有创建独立用户页表。scheduler 在
`sched_yield()` 和 `sched_from_trap()` 两条路径上都会检查新旧 task 的 `vm_space`：

```text
如果 vm_space 相同：只切换寄存器/状态，不动 satp。
如果 vm_space 不同：先 vm_activate_sv39(next->vm_space)，再恢复目标 task。
```

后续真正接入用户地址空间时，每个用户页表必须保留内核代码、内核数据、内核栈和 trap
入口所需映射。否则 scheduler 切到目标 `satp` 后，自己正在执行的内核代码或即将恢复的
内核栈会立刻失效。

## task 不是完整进程

第一版 `struct task` 只描述内核态执行流：

- task id；
- task state；
- `struct context`；
- kernel stack；
- entry 函数和参数；
- `vm_space`：当前执行流使用的地址空间；
- `sleep_timer`：用于 `task_sleep_ms()` 的一次性唤醒事件；
- `run_node`：挂入 scheduler run queue 的链表节点。

它暂时没有用户地址空间、文件描述符表、父子关系、信号、退出码、用户 trap frame。
这些内容后续会逐步把 task 扩展成真正的进程模型。

## 自检

`KERNEL_SELFTEST` 构建会创建两个内核 task。每个 task 在自己的内核栈上递增计数器并
主动 `task_yield()`，直到运行固定轮数后返回。主 selftest 循环参与调度，直到两个
task 都进入 `TASK_ZOMBIE`。

这条测试覆盖：

- 新 task 首次切换到 `task_trampoline`；
- 两个独立内核栈之间来回切换；
- `task_yield()` 的 READY/RUNNING 状态转换；
- task entry 返回后的 ZOMBIE 状态；
- 切回 selftest 主线继续执行。

sleep 自检会创建一个 task，让它调用 `task_sleep_ms(2)`。它进入 `TASK_BLOCKED` 后，
timer 到期回调会把它唤回 `TASK_READY`，主线再通过 `task_yield()` 把它调度回来。这条
测试覆盖：

- `TASK_BLOCKED` 状态；
- task 内嵌 timer event；
- timer 回调唤醒 task；
- 从 BLOCKED 回到 READY，再运行到 ZOMBIE。

另一条自检会注册一个 1ms 周期 timer。两个 task 不主动 `yield`，只在自己的入口里递增
计数器并停在 `wfi`；timer 回调只请求调度。主 selftest 等两个计数器都到达目标后取消
timer。这条测试覆盖：

- timer interrupt 触发调度请求；
- `trap_handle()` 返回另一个 task 的 `trap_frame`；
- 新 task 第一次通过初始 `trap_frame` 进入 `task_trampoline`；
- main task 被抢占后还能恢复到原来的等待循环。
