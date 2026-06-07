# 旧 SBI 直启代码

这里保存早期裸 `Image` / SBI direct boot 路径的代码，作为后续重建 trap、调度器、
用户态和内存管理时的参考。

这些文件不参与当前构建。当前默认启动路径是：

```text
OpenSBI -> U-Boot/EDK2 -> RVOS EFI app -> RVOS/KERNEL.ELF -> kernel_entry
```

旧代码可以复用思路，但不要直接接回当前内核。它依赖旧 `os.ld`、固定内存布局、
旧启动入口和旧模块边界；迁移时应按新的 `boot_info`、EFI memory map 和 kernel ELF
边界重新整理。
