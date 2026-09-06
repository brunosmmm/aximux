#!/usr/bin/env bash
# Apply AXIMUX device into a QEMU source tree and build arm-softmmu only.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QEMU_SRC="${QEMU_SRC:-$ROOT/upstream}"
BUILD="${BUILD_DIR:-$ROOT/build}"
JOBS="${JOBS:-$(nproc)}"

if [[ ! -d "$QEMU_SRC/.git" ]]; then
  echo "QEMU source missing at $QEMU_SRC" >&2
  exit 1
fi

# Prefer system ninja; QEMU needs python + pkg-config deps
command -v ninja >/dev/null
command -v pkg-config >/dev/null

cp "$ROOT/hw/aximux.c" "$QEMU_SRC/hw/misc/aximux.c"
python3 "$ROOT/scripts/patch-virt.py" "$QEMU_SRC"

mkdir -p "$BUILD"
cd "$BUILD"
if [[ ! -f build.ninja ]]; then
  "$QEMU_SRC/configure" \
    --target-list=arm-softmmu \
    --disable-docs \
    --disable-user \
    --prefix="$ROOT/install"
fi
ninja -j"$JOBS"
ninja install
echo "Built: $ROOT/install/bin/qemu-system-arm"
"$ROOT/install/bin/qemu-system-arm" -version
