#!/usr/bin/env bash
# Build a minimal ARM initramfs with busybox + aximux.ko + init.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$ROOT/../.." && pwd)"
OUT="$ROOT/guest/out"
STAGING="$OUT/initramfs"

KO="$REPO/driver/aximux.ko"
[[ -f "$KO" ]] || { echo "missing $KO — build module first" >&2; exit 1; }

mkdir -p "$OUT/dl"
BUSYBOX_SRC="${BUSYBOX_BIN:-$OUT/dl/busybox}"
if [[ ! -x "$BUSYBOX_SRC" ]]; then
  echo "Downloading static busybox-armv7l..." >&2
  URL="https://busybox.net/downloads/binaries/1.31.0-defconfig-multiarch-musl/busybox-armv7l"
  curl -fsSL -o "$OUT/dl/busybox" "$URL"
  chmod +x "$OUT/dl/busybox"
  BUSYBOX_SRC="$OUT/dl/busybox"
fi

rm -rf "$STAGING"
mkdir -p "$STAGING"/{bin,sbin,dev,proc,sys,lib/modules,etc}

cp "$BUSYBOX_SRC" "$STAGING/bin/busybox"
chmod +x "$STAGING/bin/busybox"

# Relative applets only — never use busybox --install (absolute host paths).
APPLETS="sh ash mount umount mkdir ls cat echo grep sleep poweroff reboot dmesg insmod lsmod ln rm cp mv chmod true false sync"
for a in $APPLETS; do
  ln -sf busybox "$STAGING/bin/$a"
done
ln -sf ../bin/busybox "$STAGING/sbin/poweroff"
ln -sf ../bin/busybox "$STAGING/sbin/insmod"

cp "$KO" "$STAGING/lib/modules/aximux.ko"

# Use busybox as the shebang interpreter (absolute path inside guest).
{
  echo '#!/bin/busybox ash'
  tail -n +2 "$ROOT/guest/init.sh"
} > "$STAGING/init"
chmod +x "$STAGING/init"

echo "root:x:0:0:root:/:/bin/sh" > "$STAGING/etc/passwd"

if find "$STAGING" -type l -lname '/*' | grep -q .; then
  echo "absolute symlinks in initramfs:" >&2
  find "$STAGING" -type l -lname '/*' >&2
  exit 1
fi

( cd "$STAGING" && find . | cpio -o -H newc ) | gzip -9 > "$OUT/initramfs.cpio.gz"
echo "Wrote $OUT/initramfs.cpio.gz ($(du -h "$OUT/initramfs.cpio.gz" | awk '{print $1}'))"
