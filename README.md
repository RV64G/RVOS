# RVOS

RVOS 是一个 RISC-V 教学操作系统项目。当前默认启动路径是：

```text
OpenSBI -> EDK2 / U-Boot bootefi -> RVOS EFI loader -> kernel ELF
```

## 快速开始

安装依赖后，在项目根目录运行：

```sh
make
make run
```

`make` 会构建 EFI loader 和 kernel ELF。`make run` 会生成 ESP 镜像，并在 QEMU/EDK2
里启动。

QEMU 控制台退出方式：

```text
Ctrl-A，然后 X
```

## 常用命令

```sh
make              # 构建 EFI app 和 kernel ELF
make efi-esp      # 生成 ESP 镜像
make run          # 在 QEMU/EDK2 中运行
make check-undef  # 检查 kernel ELF 是否还有未定义符号
make clean        # 清理构建产物
```

## 依赖安装

Arch/CachyOS：

```sh
./scripts/install-deps-arch.sh
```

Ubuntu/Debian：

```sh
./scripts/install-deps-ubuntu.sh
```

也可以使用 dev container。VS Code 打开项目后选择 “Reopen in Container”。

## 文档

设计文档和 wiki 草稿见 [docs/README.md](docs/README.md)。

构建、QEMU 和上板启动细节见 [docs/boot/01-build-and-run.md](docs/boot/01-build-and-run.md)。
