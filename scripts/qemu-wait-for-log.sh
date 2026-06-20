#!/usr/bin/env sh
set -eu

if [ "$#" -lt 5 ] || [ "$4" != "--" ]; then
    echo "usage: $0 LOG TIMEOUT_SECONDS MARKER -- QEMU [ARGS...]" >&2
    exit 2
fi

log_file=$1
timeout_seconds=$2
marker=$3
shift 4

mkdir -p "$(dirname "$log_file")"
rm -f "$log_file"

"$@" >"$log_file" 2>&1 &
qemu_pid=$!
start_time=$(date +%s)

while kill -0 "$qemu_pid" 2>/dev/null; do
    if grep -Fq "$marker" "$log_file" 2>/dev/null; then
        kill "$qemu_pid" 2>/dev/null || true
        wait "$qemu_pid" 2>/dev/null || true
        echo "QEMU test passed: found '$marker'"
        exit 0
    fi

    now=$(date +%s)
    if [ "$((now - start_time))" -ge "$timeout_seconds" ]; then
        kill "$qemu_pid" 2>/dev/null || true
        wait "$qemu_pid" 2>/dev/null || true
        echo "QEMU test timed out after ${timeout_seconds}s" >&2
        echo "Expected marker: $marker" >&2
        echo "---- QEMU log ----" >&2
        cat "$log_file" >&2
        exit 1
    fi

    sleep 1
done

set +e
wait "$qemu_pid"
status=$?
set -e

if grep -Fq "$marker" "$log_file" 2>/dev/null; then
    echo "QEMU test passed: found '$marker'"
    exit 0
fi

echo "QEMU exited before marker appeared, status=$status" >&2
echo "Expected marker: $marker" >&2
echo "---- QEMU log ----" >&2
cat "$log_file" >&2
exit "$status"
