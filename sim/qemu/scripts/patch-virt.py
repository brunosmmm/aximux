#!/usr/bin/env python3
"""Idempotently wire aximux into QEMU ARM virt (v11.x)."""
from __future__ import annotations

import re
import sys
from pathlib import Path

MARKER = "/* AXIMUX_QEMU_HARNESS */"

CREATE_FN = r'''
static void create_aximux(const VirtMachineState *vms)
{
    /* AXIMUX_QEMU_HARNESS */
    MachineState *ms = MACHINE(vms);
    DeviceState *dev = qdev_new("aximux");
    SysBusDevice *s = SYS_BUS_DEVICE(dev);
    hwaddr base = vms->memmap[VIRT_AXIMUX].base;
    hwaddr size = vms->memmap[VIRT_AXIMUX].size;
    char *nodename;
    int i;

    qdev_prop_set_uint32(dev, "npins", 8);
    qdev_prop_set_uint32(dev, "nalts", 4);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, base);

    nodename = g_strdup_printf("/pinctrl@%" PRIx64, base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_string(ms->fdt, nodename, "compatible",
                            "brunosmmm,aximux-2.0");
    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg", 2, base, 2, size);

    for (i = 0; i < 4; i++) {
        char *pin = g_strdup_printf("%s/pin@%d", nodename, i);
        char *fns[] = { (char *)"alt0", (char *)"alt1",
                        (char *)"alt2", (char *)"alt3" };
        qemu_fdt_add_subnode(ms->fdt, pin);
        qemu_fdt_setprop_cell(ms->fdt, pin, "reg", i);
        qemu_fdt_setprop_string_array(ms->fdt, pin, "function-names", fns, 4);
        g_free(pin);
    }
    g_free(nodename);
}

'''


def patch_virt_h(path: Path) -> None:
    text = path.read_text()
    if "VIRT_AXIMUX" in text:
        return
    text = text.replace(
        "    VIRT_ACPI_PCIHP,\n    VIRT_LOWMEMMAP_LAST,",
        "    VIRT_ACPI_PCIHP,\n    VIRT_AXIMUX,\n    VIRT_LOWMEMMAP_LAST,",
    )
    if "VIRT_AXIMUX" not in text:
        raise SystemExit("failed to insert VIRT_AXIMUX into virt.h")
    path.write_text(text)
    print(f"patched {path}")


def patch_virt_c(path: Path) -> None:
    text = path.read_text()

    if "[VIRT_AXIMUX]" not in text:
        needle = "    [VIRT_ACPI_PCIHP] =         { 0x090c0000, ACPI_PCIHP_SIZE },\n"
        insert = (
            needle
            + "    [VIRT_AXIMUX] =             { 0x090d0000, 0x00001000 },\n"
        )
        if needle not in text:
            raise SystemExit("memmap ACPI_PCIHP entry not found")
        text = text.replace(needle, insert, 1)

    if MARKER not in text:
        # Insert create_aximux before create_gpio_devices
        anchor = "static void create_gpio_devices(const VirtMachineState *vms, int gpio,\n"
        if anchor not in text:
            raise SystemExit("create_gpio_devices not found")
        text = text.replace(anchor, CREATE_FN + anchor, 1)

        call_anchor = "    create_virtio_devices(vms);\n"
        if call_anchor not in text:
            raise SystemExit("create_virtio_devices call not found")
        text = text.replace(
            call_anchor,
            "    create_aximux(vms);\n" + call_anchor,
            1,
        )

    path.write_text(text)
    print(f"patched {path}")


def patch_kconfig_misc(path: Path) -> None:
    text = path.read_text()
    if "config AXIMUX" in text:
        return
    text += "\nconfig AXIMUX\n    bool\n"
    path.write_text(text)
    print(f"patched {path}")


def patch_kconfig_arm(path: Path) -> None:
    text = path.read_text()
    if "select AXIMUX" in text:
        return
    text = text.replace(
        "config ARM_VIRT\n    bool\n",
        "config ARM_VIRT\n    bool\n    select AXIMUX\n",
        1,
    )
    if "select AXIMUX" not in text:
        raise SystemExit("failed to select AXIMUX from ARM_VIRT")
    path.write_text(text)
    print(f"patched {path}")


def patch_meson(path: Path) -> None:
    text = path.read_text()
    if "aximux.c" in text:
        return
    text = text.replace(
        "system_ss.add(when: 'CONFIG_VIRT_CTRL', if_true: files('virt_ctrl.c'))\n",
        "system_ss.add(when: 'CONFIG_VIRT_CTRL', if_true: files('virt_ctrl.c'))\n"
        "system_ss.add(when: 'CONFIG_AXIMUX', if_true: files('aximux.c'))\n",
        1,
    )
    if "aximux.c" not in text:
        raise SystemExit("failed to add aximux.c to meson.build")
    path.write_text(text)
    print(f"patched {path}")


def main() -> None:
    src = Path(sys.argv[1])
    patch_virt_h(src / "include/hw/arm/virt.h")
    patch_virt_c(src / "hw/arm/virt.c")
    patch_kconfig_misc(src / "hw/misc/Kconfig")
    patch_kconfig_arm(src / "hw/arm/Kconfig")
    patch_meson(src / "hw/misc/meson.build")


if __name__ == "__main__":
    main()
