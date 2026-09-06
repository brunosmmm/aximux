#!/bin/busybox ash
# Guest init (busybox ash). Print AXIMUX_QEMU_PASS or FAIL then power off.
set -eu

export PATH=/bin:/sbin:/usr/bin:/usr/sbin

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mkdir -p /sys/kernel/debug
mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null || true

fail() {
  echo "AXIMUX_QEMU_FAIL: $*"
  dmesg | tail -50 || true
  sync
  poweroff -f
  sleep 5
  exit 1
}

echo "== aximux qemu guest test =="

insmod /lib/modules/aximux.ko || fail "insmod"

sleep 1

PDEV=""
for d in /sys/devices/platform/*; do
  if [ -f "$d/of_node/compatible" ] && grep -q aximux "$d/of_node/compatible" 2>/dev/null; then
    PDEV="$d"
    break
  fi
done
[ -n "$PDEV" ] || fail "no aximux platform device"
[ -f "$PDEV/srcsel" ] || fail "missing srcsel sysfs"

echo "srcsel before:"
cat "$PDEV/srcsel" || fail "read srcsel"

DBG=""
for d in /sys/kernel/debug/pinctrl/*; do
  [ -d "$d" ] || continue
  case "$d" in
    *90d0000*|*aximux*|*pinctrl@*) DBG="$d" ;;
  esac
done
[ -n "$DBG" ] || fail "no pinctrl debugfs dir (ls=$(ls /sys/kernel/debug/pinctrl 2>/dev/null || true))"
[ -f "$DBG/pinmux-select" ] || fail "no pinmux-select in $DBG"

# Generic pinctrl debugfs: "group function" (groups are pin0..pinN)
echo "pin0 alt1" > "$DBG/pinmux-select" || fail "pinmux-select write"

AFTER="$(cat "$PDEV/srcsel")"
echo "srcsel after:"
echo "$AFTER"
echo "$AFTER" | grep -q "pin0:0x01" || fail "expected pin0:0x01, got: $AFTER"

echo "AXIMUX_QEMU_PASS"
sync
poweroff -f
sleep 5
