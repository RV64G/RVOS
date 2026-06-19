# Syscall 与最小用户态

这一页说明当前第一条用户态闭环。它还不是完整进程模型，也没有从文件系统加载 ELF；
目标是先证明：

```text
U-mode 程序
  -> ecall
  -> S-mode trap
  -> syscall 分发
  -> 返回或退出
```

## 调用链

普通启动路径在 IRQ、timer、console 初始化完成后进入用户态 demo：

```text
kernel_entry()
  -> user_demo_run()：准备最小用户地址空间、用户 section、用户栈和用户 trap 栈。
     -> vm_space_create()：创建 user_demo_vm。
     -> vm_copy_kernel_mappings()：复制内核 S-mode 映射，不继承 U-mode 映射。
     -> map_user_section()：把 .user section 映射到 user_demo_vm，权限为 U/R/X。
     -> phys_alloc_pages()：分配用户栈和 U-mode trap 栈。
     -> vm_map_range()：把用户栈映射到 user_demo_vm 的用户虚拟地址。
     -> vm_activate_sv39()：切到 user_demo_vm。
     -> riscv_enter_user()：设置 sepc/sstatus/sscratch，然后 sret 到 U-mode。
```

用户程序执行：

```text
user_demo_start()
  -> ecall write
  -> ecall exit
```

trap 返回路径：

```text
trap_vector()
  -> 如果来自 U-mode：从 sscratch 切到内核 trap 栈。
  -> 保存 trap_frame。
  -> trap_handle()
     -> syscall_handle()
  -> 恢复 trap_frame。
  -> sret
```

## .user section

当前用户程序直接链接进 kernel ELF 的 `.user` section。链接脚本把这个 section 独立按
页对齐，原因是 RISC-V 页表的用户权限是按页生效的：

```text
.text/.rodata/.data/.bss : 内核权限
.user                    : U/R/X，允许 U-mode 取指和读取常量
```

这不是最终程序加载方案。后续 ELF loader 出现后，用户程序会来自文件系统，`.user`
section 可以退化成测试或早期 bring-up 用例。

当前 `.user` section 中只放一个很小的用户入口。它不走 C 运行时，也不依赖用户
libc，只是直接按 Linux RISC-V syscall ABI 发起 `write` 和 `exit`，用来验证 U-mode
入口、trap 换栈和 syscall 分发。

`.user` 不再映射进 `kernel_vm_space()`。当前 demo 会创建一张独立的
`user_demo_vm`，先复制必要的内核映射，再只在这张用户页表里加入 `.user` 和用户栈。
这仍然不是完整进程地址空间，但已经不再和 boot task 共用同一张根页表。

## 用户栈和 trap 栈

用户栈和 trap 栈是两件事。

用户栈：

```text
U-mode sp 使用的栈
映射成 U/R/W
用户程序可以读写
```

trap 栈：

```text
S-mode 保存 trap_frame 使用的内核栈
不映射 U 位
用户程序不能直接访问
```

RISC-V 进入 trap 时不会自动换栈。如果从 U-mode 进入 trap 时仍使用用户 sp，内核就会
把寄存器现场写到用户栈上。当前 `trap_vector()` 用 `sscratch` 保存内核 trap 栈顶：

```text
U-mode 正常运行:
  sscratch = kernel trap stack top

U-mode trap 进入:
  csrrw sp, sscratch, sp
  sp       = kernel trap stack top
  sscratch = interrupted user sp
```

保存 `trap_frame.sp` 时，会把 `sscratch` 里的用户 sp 写进去。返回 U-mode 前，再把
`sscratch` 恢复成内核 trap 栈顶，供下一次用户 trap 使用。

## syscall ABI

当前 syscall 编号按 Linux RISC-V ABI 取值，先只实现最小集合：

```text
a7      : syscall number
a0..a5  : 最多 6 个参数
a0      : 返回值
```

已接入：

```text
write = 64
exit  = 93
```

`ecall` 是固定 32-bit 指令。syscall 处理完后必须执行：

```text
sepc += 4
```

否则 `sret` 回到用户态后会再次执行同一条 `ecall`。

## 用户指针复制

`write(fd, user_buf, count)` 需要从用户页读取数据。RISC-V 默认不允许 S-mode 直接访问
带 U 位的用户页；内核需要临时打开 `sstatus.SUM`：

```text
保存旧 sstatus
关闭 SIE，避免 SUM=1 时被中断 handler 打断
打开 SUM
读取用户缓冲区
恢复旧 SUM 状态
恢复旧 SIE 状态
```

当前 `copy_from_user()` / `copy_to_user()` 已经独立成公共 helper，`sys_write()` 通过
它把用户缓冲区分块复制到内核栈缓冲区，再输出到 `printk`。

这还不是最终安全边界：当前 helper 不能从 page fault 中恢复，也没有完整用户地址空间
边界检查。等用户 `vm_space` 和 task 生命周期稳定后，这里要继续补用户范围校验、部分
复制语义和 fault 恢复路径。

## 当前边界

当前已经证明 U-mode 能通过 `ecall` 进入内核并调用 `write/exit`。还没有实现：

- 用户 task / PCB 生命周期。
- 完整用户地址空间布局和销毁路径。
- ELF 用户程序加载。
- `read/open/close/fork/exec/wait`。
- 用户异常隔离和进程回收。

下一步如果继续沿着用户态主线推进，优先级应该是用户 task 生命周期、用户地址空间销毁、
用户栈/程序映射所有权，再到 ELF loader 和文件系统。
