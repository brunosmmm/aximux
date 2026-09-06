#!/usr/bin/env bash
# Build guest zImage (multi_v7 from 6.18.10), aximux.ko, and initramfs.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Re-exec under a clean env so a previously sourced Poky SDK cannot poison HOSTCC
# (C_INCLUDE_PATH / --sysroot leakage breaks certs/extract-cert).
if [[ "${AXIMUX_CLEAN_ENV:-}" != "1" ]]; then
  exec env -i \
    AXIMUX_CLEAN_ENV=1 \
    HOME="${HOME:-/home/bruno}" \
    USER="${USER:-bruno}" \
    JOBS="${JOBS:-$(nproc)}" \
    KERNEL_SRC="${KERNEL_SRC:-}" \
    CROSS_COMPILE="${CROSS_COMPILE:-}" \
    SDK_CROSS_BIN="${SDK_CROSS_BIN:-}" \
    PATH="/usr/bin:/bin" \
    bash "$0" "$@"
fi

# ROOT is …/aximux/sim/qemu → repo root is ../..
REPO="$(cd "$ROOT/../.." && pwd)"
OUT="$ROOT/guest/out"
JOBS="${JOBS:-$(nproc)}"

KERNEL_SRC="${KERNEL_SRC:-/home/bruno/work/ebaz4205/build-scarthgap/tmp/work-shared/ebaz4205-zynq7/kernel-source}"
SDK_CROSS_BIN="${SDK_CROSS_BIN:-/home/bruno/ebaz/sysroots/x86_64-pokysdk-linux/usr/bin/arm-poky-linux-gnueabi}"
HOST_BIN="$ROOT/scripts/host-bin"
mkdir -p "$HOST_BIN"
ln -sfn "$ROOT/scripts/host-bc" "$HOST_BIN/bc"
export PATH="$HOST_BIN:$SDK_CROSS_BIN:/usr/bin:/bin"
CROSS="${CROSS_COMPILE:-arm-poky-linux-gnueabi-}"

if [[ ! -d "$KERNEL_SRC" ]]; then
  echo "KERNEL_SRC missing: $KERNEL_SRC" >&2
  exit 1
fi
if ! command -v "${CROSS}gcc" >/dev/null 2>&1; then
  echo "cross gcc missing: ${CROSS}gcc (PATH=$PATH)" >&2
  exit 1
fi
"${CROSS}gcc" --version | head -1
cc --version | head -1

mkdir -p "$OUT"
KBUILD="$OUT/kbuild"
mkdir -p "$KBUILD"

if [[ ! -f "$KBUILD/.config" ]]; then
  make -C "$KERNEL_SRC" O="$KBUILD" ARCH=arm CROSS_COMPILE="$CROSS" multi_v7_defconfig
  "$ROOT/scripts/merge-guest-config.sh" "$KBUILD/.config"
  make -C "$KERNEL_SRC" O="$KBUILD" ARCH=arm CROSS_COMPILE="$CROSS" olddefconfig
fi

echo "==> Building kernel"
make -C "$KERNEL_SRC" O="$KBUILD" ARCH=arm CROSS_COMPILE="$CROSS" -j"$JOBS" zImage modules

echo "==> Building aximux.ko"
make -C "$REPO/driver" KERNEL_SRC="$KBUILD" ARCH=arm CROSS_COMPILE="$CROSS" -j"$JOBS"

echo "==> Assembling initramfs"
"$ROOT/scripts/mk-initramfs.sh"

echo "Guest artifacts in $OUT"
ls -la "$OUT"
