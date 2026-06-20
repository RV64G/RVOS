# libfdt

This directory vendors the read-only subset of `libfdt` from the Device Tree
Compiler project.

Upstream: https://github.com/dgibson/dtc

License: `GPL-2.0-or-later OR BSD-2-Clause`, as recorded by the SPDX headers in
the copied source files.

Only the files needed by the kernel DTB reader are included:

- `fdt.c`
- `fdt_ro.c`
- `fdt_strerror.c`
- `fdt.h`
- `libfdt.h`
- `libfdt_env.h`
- `libfdt_internal.h`

`libfdt_env.h` is minimally adjusted for the freestanding kernel environment.
