#ifndef KERNEL_SELFTEST_H
#define KERNEL_SELFTEST_H

/**
 * 运行内核基础设施自检。
 *
 * 当前只在 KERNEL_SELFTEST 构建中由 kernel_entry() 调用。测试失败返回 0，并在日志
 * 中打印失败项；测试通过返回 1，调用侧可以打印 CI 使用的完成标记。
 */
int kernel_selftest_run(void);

#endif
