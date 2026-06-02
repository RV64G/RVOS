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
- [07 启动链后续工作](boot/07-next-steps.md)

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

设计文档建议固定包含：

1. 目标
2. 背景知识
3. 关键数据结构
4. 控制流程
5. 需要完成的工作
6. 验证方式
7. 注意事项
