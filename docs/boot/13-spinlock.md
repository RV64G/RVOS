# Spinlock 与中断状态

这一页说明当前内核自旋锁。它来自旧代码里的思路，但重新接到现在的 kernel 目录和
RISC-V AMO 实现上。

## 调用链

```text
spinlock_init()
  -> 清 locked，记录调试名字。

spinlock_acquire()
  -> irq_save()：用 csrrc 保存当前 sstatus，并关闭当前 hart 的 S-mode 中断。
  -> atomic_swap_acquire()：用 amoswap.w.aq 抢锁。
  -> 抢不到时自旋等待 locked 变成 0。

spinlock_release()
  -> atomic_store_release()：用 amoswap.w.rl 把 locked 写回 0。
  -> irq_restore()：恢复获取锁前的 S-mode 中断状态。
```

## 为什么拿锁前要关中断

自旋锁不只服务多核，也服务“普通内核代码”和“中断处理代码”之间的互斥。

如果同一个 hart 正在持有某把锁，随后被中断打断，而中断处理函数又尝试获取同一把锁，
就会变成：

```text
普通代码持锁
  -> 中断进入
     -> 中断处理函数等待同一把锁
        -> 但普通代码无法继续运行并释放锁
```

这就是单 hart 上的死锁。`spinlock_acquire()` 先关闭当前 hart 的 S-mode 中断，可以避免
当前 hart 在持锁期间被本地中断路径再次打断。

`irq_save()` 保存的是进入锁之前的 `sstatus`。释放锁时，`irq_restore()` 按保存值恢复
中断状态。这样如果调用者本来就在关中断环境里，释放锁不会意外打开中断。

`irq_save()` 使用 `csrrc`，而不是先读 `sstatus` 再单独清位：

```text
csrrc old, sstatus, SSTATUS_SIE
```

它在一条指令里完成“读旧值”和“清 SIE 位”。这里的原子性只针对当前 hart 的 CSR 操作，
不是多核内存原子；但这正好符合关本 hart 中断的需求。

## AMO 和内存顺序

`locked` 是一个 32-bit 字段：

```text
locked = 0  空闲
locked = 1  已持有
```

获取锁使用：

```text
amoswap.w.aq old, 1, (locked)
```

它原子地读取旧值并写入 1。旧值为 0 表示这次成功拿到锁；旧值非 0 表示别人已经持有，
当前 hart 继续自旋。

`.aq` 是 acquire 语义：拿到锁之后，临界区里的内存访问不能被重排到抢锁之前。

释放锁使用：

```text
amoswap.w.rl x0, 0, (locked)
```

`.rl` 是 release 语义：临界区里的内存访问不能被重排到解锁之后。其它 hart 看到锁变成
0 后，也能看到解锁前完成的临界区写入。

`aq/rl` 不是只给“本 hart 自娱自乐”的标记。它们首先约束执行这条 AMO 的 hart 上的内存
顺序，但所有 hart 都用同一把锁、同一套 acquire/release 规则时，就形成跨 hart 同步：

```text
hart A:
  acquire(lock)
  写共享数据
  release(lock)

hart B:
  acquire(lock)
  读共享数据
  release(lock)
```

`hart A` 的 release 保证共享数据写入先于解锁对外可见；`hart B` 的 acquire 保证它看到
锁可获取之后，后续读共享数据不会被提前到拿锁之前。两边配合起来，才得到“拿到同一把
锁后能看到前一个持锁者的临界区结果”。

如果某个 hart 不走这把锁，直接读写共享数据，那 `aq/rl` 不会替它建立顺序。锁保护的
前提仍然是所有访问者都遵守同一个锁协议。

## 当前边界

这把锁只是互斥原语，不会自动让现有内核变成 SMP safe。

后续要把多核真正打开，还需要继续处理：

- per-hart `current_task` 和 trap stack。
- `run_queue`、`timer_list`、`kmalloc`、page allocator 的锁粒度。
- `printk` / UART 输出互斥。
- PLIC context、timer deadline 和调度状态的 per-hart 初始化。

因此当前阶段适合先把 spinlock 作为基础能力放进去，再逐个给共享结构加锁，而不是一次
性启动所有 hart。
