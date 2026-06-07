#!/usr/bin/env bash
set -euo pipefail

tftp_root="${TFTP_ROOT:-/tmp/rvos-tftp}"
tftp_iface="${TFTP_IFACE:-enp55s0}"
tftp_host="${TFTP_HOST:-10.90.50.43}"

if ! command -v dnsmasq >/dev/null 2>&1; then
    echo "error: dnsmasq not found" >&2
    exit 1
fi

if [[ ! -f "$tftp_root/BOOTRISCV64.EFI" || ! -f "$tftp_root/KERNEL.ELF" ]]; then
    echo "error: TFTP artifacts not found in $tftp_root" >&2
    echo "run: make tftp-sync" >&2
    exit 1
fi

if ! ip -4 addr show dev "$tftp_iface" | grep -q "inet $tftp_host/"; then
    echo "error: $tftp_iface does not have address $tftp_host" >&2
    echo >&2
    echo "For a direct cable to the board, configure it with:" >&2
    echo "  sudo ip link set $tftp_iface up" >&2
    echo "  sudo ip addr replace $tftp_host/24 dev $tftp_iface" >&2
    echo >&2
    echo "Then run:" >&2
    echo "  make tftp-serve TFTP_IFACE=$tftp_iface TFTP_HOST=$tftp_host" >&2
    exit 1
fi

cat <<EOF
Starting TFTP server:
  interface: $tftp_iface
  host:      $tftp_host
  root:      $tftp_root

Keep this terminal open while U-Boot downloads files.
EOF

exec sudo dnsmasq \
    --no-daemon \
    --port=0 \
    --interface="$tftp_iface" \
    --listen-address="$tftp_host" \
    --bind-interfaces \
    --enable-tftp \
    --tftp-root="$tftp_root" \
    --log-facility=-
