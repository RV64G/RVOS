# SBI Timer 与 Timer List

Trap 入口安装后，内核可以开始接周期性时钟中断。当前 timer 仍通过 SBI 提供的
`set_timer` 接口完成。timer list 的到期判断使用 `rdtime` cycles。调度 tick 暂时不在
这里提前实现，等进程切换和调度器出现后，再由调度器自己注册周期 timer。

## 调用链

```text
kernel_entry()
  -> trap_init()：安装统一 trap 入口。
  -> timer_init()：读取 timebase_frequency，初始化 timer list。
     -> sbi_set_timer(deadline)：请求下一次 S-mode timer interrupt。
     -> csr_set_sie(SIE_STIE)：允许 supervisor timer interrupt。
     -> csr_set_sstatus(SSTATUS_SIE)：打开 S-mode 全局中断。

trap_vector
  -> trap_handle(frame)
     -> handle_interrupt(frame, code)
        -> timer_handle_interrupt()
           -> timer_run_due_events()：按 rdtime cycles 处理已经到期的 timer event。
           -> sbi_set_timer(deadline)：设置下一次硬件 timer interrupt。
```

## timebase_frequency

`timebase_frequency` 来自 DTB，表示 `rdtime` 每秒增长多少次。QEMU virt 常见值是
`10000000`，也就是 10MHz。

timer list 会把上层传入的毫秒值换算成 `rdtime` cycles。比如 timebase 是 10MHz 时，
1000ms 就是 10,000,000 cycles。

## 外部 timer

timer 核心只提供注册接口，不内置每秒日志、调度 tick 或其他业务行为。外部模块可以
这样注册周期 timer event：

```text
some_timer:
  delay_ms  = 1000
  period_ms = 1000
```

后续如果需要调试输出、自测事件、sleep、IO timeout 或调度时间片，都应该通过这套接口
注册。debug/test 怎么组织单独讨论，不放进 timer 核心。

## cycles 和 timer event

当前 timer 子系统只用一种时间判断事件到期：

```text
rdtime cycles:
  硬件单调时间。timer list、sleep、timeout 使用它判断是否到期。
```

调度 tick 暂时不做。以后做 RR 时间片时，可以由调度器注册一个周期 timer，或者在调度
模块里单独维护 tick 计数。但 sleep、timeout、周期任务仍应该继续使用 `rdtime` cycles，
避免把“内核处理过多少次调度节拍”和“真实时间过去多少”混在一起。

硬件 timer 只有一个下一次 deadline，所以每次重新编程时直接使用 timer list 头节点的
`deadline_cycles`：

```text
timer_list:
  debug_timer   -> 调试模块持有
  sleep_timer   -> 上层对象持有
  timeout_timer -> 上层对象持有
```

这样代码里只有一种到期机制。后续调度器如果需要 10ms 时间片，也应该通过同一套机制
注册，而不是让 timer 核心提前内置调度策略。

## timer list

当前 timer list 是一个按 `deadline_cycles` 从小到大排序的单链表。每个事件节点由调用
者持有：

```c
struct timer_event {
    struct timer_event *next;
    uint64_t deadline_cycles;
    uint64_t period_cycles;
    timer_callback_t callback;
    void *context;
    int active;
};
```

上层使用毫秒接口：

```c
timer_schedule_ms(event, delay_ms, period_ms);
```

`timer_schedule_ms()` 会根据 `timebase_frequency` 把毫秒换算成 cycles。内部没有直接
用 `milliseconds * timebase_frequency`，而是拆成整秒和剩余毫秒，避免长延时乘法溢出。

## 毫秒到 cycles 的换算

直觉公式是：

```text
cycles = ceil(milliseconds * timebase_frequency / 1000)
```

这里需要 `ceil`，是因为 timer 不能早于请求时间触发。比如 `timebase_frequency = 32768`
时，1ms 对应 32.768 cycles；如果直接整数除法得到 32 cycles，timer 会稍微提前。

不能简单先算：

```text
cycles_per_ms = timebase_frequency / 1000
cycles = milliseconds * cycles_per_ms
```

因为如果 timebase 不能被 1000 整除，误差会随毫秒数累积。32768Hz 下整数
`cycles_per_ms` 是 32，但真实值是 32.768；1000ms 后会少算 768 cycles。

当前实现把毫秒拆成：

```text
seconds      = milliseconds / 1000
remaining_ms = milliseconds % 1000
```

整秒部分直接乘 `timebase_frequency`。剩余毫秒一定小于 1000，所以可以直接先乘
`timebase_frequency`，再除以 1000 并向上取整：

```text
ceil(remaining_ms * timebase_frequency / 1000)
```

这样不会像 `remaining_ms * (timebase_frequency / 1000)` 那样先截断每毫秒 cycles。

代码里没有直接写 `(product + 999) / 1000`，而是用除法和取余做同样的向上取整：

```text
ceil(x / 1000) = (x + 999) / 1000
x / 1000 + (x % 1000 != 0)
```

这样可以避免 `product + 999` 在 `product` 接近 `UINT64_MAX` 时溢出。

这样既避免大乘法溢出，也避免因为截断导致 timer 提前触发。

timer 子系统不在 `timer_schedule_ms()` 里偷偷 `kmalloc()`，也不会在事件触发后
`kfree()`。这样做有两个原因：

- timer interrupt 属于中断上下文，第一版先避免在中断里做堆分配和释放。
- 后续进程睡眠、IO 超时、调度时间片都可以把 `struct timer_event` 直接嵌进自己的
  PCB 或等待对象里，生命周期由上层对象管理。

`period_ms == 0` 表示一次性 timer；`period_ms != 0` 表示周期 timer。周期 timer 触发时
会按原 deadline 推进到下一次未来时间点，而不是简单使用 `now + period`。这样短暂延迟
不会造成长期漂移；如果错过多个周期，只执行一次回调，并把下一次 deadline 对齐到未来。

周期 timer 会先重新挂回链表，然后再执行回调。如果回调想停止这个周期 timer，可以调用
`timer_cancel(event)`。

当前回调仍然直接在 timer interrupt 里运行，所以回调必须很短，不能长时间打印、等待
锁、阻塞或者做复杂工作。等调度器和软中断/工作队列成形后，可以把耗时逻辑从硬中断
路径里移出去。
