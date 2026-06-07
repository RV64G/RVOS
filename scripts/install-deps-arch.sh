#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pkg_file="$repo_root/tools/deps/arch-pacman.txt"

if ! command -v pacman >/dev/null 2>&1; then
    echo "error: pacman not found; this script is for CachyOS/Arch Linux" >&2
    exit 1
fi

if [[ ! -f "$pkg_file" ]]; then
    echo "error: package list not found: $pkg_file" >&2
    exit 1
fi

mapfile -t packages < <(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' "$pkg_file")

sudo pacman -Syu --needed "${packages[@]}"

cat <<'EOF'

Installed RVOS host dependencies.

Optional Rust setup for later syscall generation:

  curl https://sh.rustup.rs -sSf | sh
  source "$HOME/.cargo/env"
  rustup component add rustfmt clippy
  rustup target add riscv64gc-unknown-none-elf

EOF
