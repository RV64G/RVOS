# RVOS

A simple RISC-V operating system implementation.

## 文档

详细设计和路线图见 [docs/README.md](docs/README.md)。

## 快速开始

1. 准备环境
   - 安装 [Docker](https://www.docker.com/) 并确保服务正在运行。
   - 安装 [Visual Studio Code](https://code.visualstudio.com/) 并添加 "Dev Containers" 扩展。

2. 打开项目容器
   - 在 VS Code 中点击左下角的“Reopen in Container”按钮。
   - VS Code 会根据 `.devcontainer` 配置构建并启动容器。

3. 构建并运行
   - 在容器终端中运行 `make` 构建 EFI app 和 kernel ELF。
   - 运行 `make efi-esp` 生成 ESP 镜像。
   - 运行 `make run` 在 QEMU/EDK2 中启动。

4. 调试
   - 当前默认调试目标是 JTAG 调试，程序文件为 `build/kernel/kernel.elf`。
   - QEMU EFI 路径的 GDB 调试入口后续重新接入。

5. 关闭 QEMU
   - `make run` 使用 QEMU 控制台运行，按 `Ctrl-A` 再按 `X` 退出。

6. 清理构建产物（可选）
   - 在项目根目录运行 `make clean`。
