#!/usr/bin/env bash
# Boot QEMU virt + aximux MMIO and require AXIMUX_QEMU_PASS on console.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/guest/out"
QEMU="${QEMU:-$ROOT/install/bin/qemu-system-arm}"
TIMEOUT_SEC="${TIMEOUT_SEC:-120}"

[[ -x "$QEMU" ]] || { echo "missing qemu: $QEMU" >&2; exit 1; }
[[ -f "$OUT/kbuild/arch/arm/boot/zImage" ]] || { echo "missing zImage — run build-guest.sh" >&2; exit 1; }
[[ -f "$OUT/initramfs.cpio.gz" ]] || { echo "missing initramfs — run build-guest.sh" >&2; exit 1; }

LOG="$OUT/qemu-console.log"
rm -f "$LOG"

# cortex-a15 is the usual 32-bit virt CPU; matches multi_v7
set +e
timeout --foreground "$TIMEOUT_SEC" "$QEMU" \
  -M virt \
  -cpu cortex-a15 \
  -m 512 \
  -nographic \
  -no-reboot \
  -kernel "$OUT/kbuild/arch/arm/boot/zImage" \
  -initrd "$OUT/initramfs.cpio.gz" \
  -append "console=ttyAMA0 earlycon=pl011,0x09000000 rdinit=/init panic=1" \
  | tee "$LOG"
rc=${PIPESTATUS[0]}
set -e

if grep -q "AXIMUX_QEMU_PASS" "$LOG"; then
  echo "PASS: AXIMUX QEMU harness"
  exit 0
fi

echo "FAIL: no AXIMUX_QEMU_PASS (qemu exit=$rc)" >&2
grep -E "AXIMUX_QEMU_|Kernel panic|Oops" "$LOG" | tail -20 >&2 || true
exit 1
