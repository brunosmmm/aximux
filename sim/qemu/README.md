# AXIMUX QEMU virt harness (AXIMUX-0003)

Validates the pinctrl driver against a fake AXIMUX MMIO model on
`qemu-system-arm -M virt` (upstream QEMU 11.x), booting Linux **6.18.10**
(armv7 multi_v7) matching the board kernel series.

## Quick start

```bash
# once: build patched QEMU (arm-softmmu + aximux device)
./scripts/build-qemu.sh

# once: build guest zImage + aximux.ko + initramfs (needs Poky SDK cross)
./scripts/build-guest.sh

# run automated guest checks
./scripts/run-test.sh
# or: make -C sim/qemu test
```

Requires: `dtc`, host `gcc`/`ninja`/`pkg-config`, Poky arm cross at
`/home/bruno/ebaz/sysroots/...` (override with `SDK_CROSS_BIN` /
`CROSS_COMPILE`), and kernel source at `KERNEL_SRC` (default: scarthgap
`kernel-source` for 6.18.10).

## Layout

| Path | Role |
|------|------|
| `hw/aximux.c` | QEMU sysbus MMIO model (MUXINFO + SRCSEL) |
| `scripts/patch-virt.py` | Wires device into ARM virt @ `0x090d0000` + DT |
| `guest/init.sh` | Guest test (insmod, pinmux-select, srcsel) |
| `upstream/` | Shallow QEMU v11.0.1 (gitignored build tree) |
| `install/` | Built `qemu-system-arm` |
| `guest/out/` | zImage, initramfs, logs |

## Notes

- Board deploy is **armv7 / Zynq**, not aarch64 — this harness matches that ABI.
- Do **not** `source` the Poky `environment-setup-*` script before `build-guest.sh`;
  the script uses a clean `env -i` so HOSTCC is not poisoned by the target sysroot.
