#ifndef KERNEL_USER_H
#define KERNEL_USER_H

/**
 * 准备并运行当前内嵌的最小用户态 demo。
 *
 * 第一版用户程序不来自文件系统，而是链接在 kernel ELF 的 .user section 中。
 */
int user_demo_run(void);

#endif
