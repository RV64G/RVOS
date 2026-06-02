# 启动流程总览

RVOS 当前默认走 EFI 启动路径。它不是让内核直接被固件当作裸镜像执行，而是先运行
一个很小的 EFI loader，再由 loader 装载真正的 kernel ELF。

启动链路可以先按这条时间线理解：

```text
OpenSBI
  -> U-Boot bootefi / QEMU EDK2
  -> RVOS EFI app
  -> RVOS/KERNEL.ELF
  -> kernel_entry(boot_info)
```

OpenSBI 负责把机器带到 S-mode，并提供 SBI 调用。U-Boot 或 EDK2 负责提供 UEFI
Boot Services。RVOS EFI app 负责和固件交互：打印早期日志、打开 ESP 文件系统、
读取 kernel ELF、准备 `boot_info`、获取最终 memory map、退出 Boot Services。

退出 Boot Services 之后，固件分配接口失效，控制权进入 RVOS 内核。内核入口收到
`boot_info`，先校验启动 ABI，再整理 memory map，建立物理页分配器，最后打开第一张
Sv39 页表。

这一阶段的目标不是立刻拥有完整内核，而是把启动边界固定下来：

```text
固件负责启动服务
EFI loader 负责交接材料
kernel ELF 负责长期运行
```

后续 trap、设备驱动、文件系统、用户程序加载和 Linux syscall 兼容，都应该建立在
这条边界清楚的启动链之上。
