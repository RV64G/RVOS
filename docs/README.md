# RVOS 文档索引

这里作为项目文档和 Wiki 草稿的主目录。稳定后的页面可以同步到 GitHub Wiki，
但仓库内的 `docs/` 仍然应保留一份，避免文档和代码脱节。

## 阅读入口

- [启动流程总览](boot/00-overview.md)
- [构建与运行](boot/01-build-and-run.md)
- [项目路线图](project-roadmap.md)

## 启动链文档

启动相关文档按时间线阅读：

- [00 启动流程总览](boot/00-overview.md)
- [01 构建与运行](boot/01-build-and-run.md)
- [02 EFI Loader](boot/02-uefi-loader.md)
- [03 Boot Info](boot/03-boot-info.md)
- [04 Kernel Entry](boot/04-kernel-entry.md)
- [05 Memory Map 与物理页分配](boot/05-memory-map-and-pages.md)
- [06 第一张 Sv39 页表](boot/06-first-page-table.md)
- [07 内存接口](boot/07-memory-interfaces.md)
- [08 平台 MMIO 与 UART 输出](boot/08-platform-init.md)
- [09 Trap 入口与返回](boot/09-trap-entry.md)
- [10 SBI Timer 与 Timer List](boot/10-sbi-timer.md)
- [11 内核 Task 与上下文切换](boot/11-kernel-task.md)

## 后续专题

这些页面等对应功能推进时再补，不需要现在写满：

- `trap-and-syscall/`：trap、异常处理、系统调用 ABI。
- `scheduler/`：任务模型、状态转换、RR/FCFS、等待队列。
- `filesystem/`：VFS、RAMFS、fd 表、路径解析。
- `elf-loader/`：ELF 加载、用户栈、exec。
- `linux-compat/`：Linux RISC-V syscall 兼容和 BusyBox。
- `test-report/`：验收清单和演示脚本。

## 写法约定

项目文档统一写在 `docs/` 目录。源码注释只补真正需要贴近实现的位置：

- 硬件或 ABI 约束，例如寄存器、CSR、trap 返回约定。
- 链接脚本、启动入口、上下文切换等不容易从 C 代码看出的细节。
- 容易被误改的边界条件和实现限制。

不要把路线图、模块总览、长调用链、验收说明写进源码注释里；这些内容放到
`docs/`。

启动链文档按函数调用流程写。函数名后面只用一句话说明“做什么”，具体“怎么做”
放在正文或源码注释里：

```text
efi_main()
  -> efi_collect_boot_info()：收集 DTB、boot hart id 和 memory map。
     -> find_dtb()：从 EFI configuration table 找 DTB。
  -> efi_load_kernel_elf()：打开并装载 kernel ELF。
```

写调用链时只列关键函数：

- 启动主线函数必须列。
- 解释关键机制的二级函数可以列。
- 涉及 ABI、硬件格式、页表结构、内存所有权的三级函数可以列。
- 纯 helper 不列，例如 `align_up()`、普通字符串比较和简单打印函数。

源码注释优先使用 Doxygen 风格写在头文件接口上，方便编辑器悬停提示：

```c
/**
 * 分配连续物理页。
 *
 * @param pages 需要分配的 4KB 页数量，必须大于 0。
 * @return 成功时返回物理地址；失败时返回 0。
 */
void *phys_alloc_pages(uint64_t pages);
```

实现文件里的大块注释只解释第一眼看不出的约束，例如 `ExitBootServices()` 前为什么
必须重新读取 memory map、PTE 的 A/D 位为什么预置、页分配器 metadata 为什么要从
usable memory 里切出来。
