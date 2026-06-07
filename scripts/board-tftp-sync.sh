#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tftp_root="${TFTP_ROOT:-/tmp/rvos-tftp}"

mkdir -p "$tftp_root"

(
    cd "$repo_root"
    make all
    make check-undef
)

install -m 0644 "$repo_root/build/efi/BOOTRISCV64.EFI" "$tftp_root/BOOTRISCV64.EFI"
install -m 0644 "$repo_root/build/kernel/kernel.elf" "$tftp_root/KERNEL.ELF"

cat <<EOF
TFTP artifacts are ready:
  $tftp_root/BOOTRISCV64.EFI
  $tftp_root/KERNEL.ELF
EOF
