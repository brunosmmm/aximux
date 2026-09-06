/*
 * AXIMUX — fake AXI soft-IP MMIO for QEMU virt driver testing.
 *
 * Register map (little-endian 32-bit):
 *   SRCSEL[n] @ n*4  — SRC[3:0], SHORT(5), DIREN(6), DIRCTL(7)
 *   MUXINFO   @ 0x80 — [7:0]=npins, [15:8]=nalts
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"

#define TYPE_AXIMUX "aximux"
OBJECT_DECLARE_SIMPLE_TYPE(AximuxState, AXIMUX)

#define AXIMUX_REG_SIZE     0x1000
#define AXIMUX_REG_MUXINFO  0x80
#define AXIMUX_MAX_PINS     32

struct AximuxState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t srcsel[AXIMUX_MAX_PINS];
    uint32_t muxinfo;
    uint32_t npins;
    uint32_t nalts;
};

static uint64_t aximux_read(void *opaque, hwaddr addr, unsigned size)
{
    AximuxState *s = opaque;
    uint32_t idx;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "aximux: bad read size %u @ 0x%" HWADDR_PRIx "\n",
                      size, addr);
        return 0;
    }

    if (addr == AXIMUX_REG_MUXINFO) {
        return s->muxinfo;
    }

    if (addr < AXIMUX_REG_MUXINFO && (addr & 3) == 0) {
        idx = addr >> 2;
        if (idx < s->npins) {
            return s->srcsel[idx];
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "aximux: read unmapped @ 0x%" HWADDR_PRIx "\n", addr);
    return 0;
}

static void aximux_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    AximuxState *s = opaque;
    uint32_t idx;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "aximux: bad write size %u @ 0x%" HWADDR_PRIx "\n",
                      size, addr);
        return;
    }

    if (addr == AXIMUX_REG_MUXINFO) {
        return;
    }

    if (addr < AXIMUX_REG_MUXINFO && (addr & 3) == 0) {
        idx = addr >> 2;
        if (idx < s->npins) {
            s->srcsel[idx] = (uint32_t)val;
            return;
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "aximux: write unmapped @ 0x%" HWADDR_PRIx "\n", addr);
}

static const MemoryRegionOps aximux_ops = {
    .read = aximux_read,
    .write = aximux_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void aximux_realize(DeviceState *dev, Error **errp)
{
    AximuxState *s = AXIMUX(dev);

    if (s->npins == 0 || s->npins > AXIMUX_MAX_PINS) {
        error_setg(errp, "aximux: npins must be 1..%d", AXIMUX_MAX_PINS);
        return;
    }
    if (s->nalts == 0 || s->nalts > 15) {
        error_setg(errp, "aximux: nalts must be 1..15");
        return;
    }

    s->muxinfo = (s->nalts << 8) | s->npins;
    memset(s->srcsel, 0, sizeof(s->srcsel));

    memory_region_init_io(&s->iomem, OBJECT(s), &aximux_ops, s,
                          TYPE_AXIMUX, AXIMUX_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static const VMStateDescription vmstate_aximux = {
    .name = TYPE_AXIMUX,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(srcsel, AximuxState, AXIMUX_MAX_PINS),
        VMSTATE_UINT32(muxinfo, AximuxState),
        VMSTATE_UINT32(npins, AximuxState),
        VMSTATE_UINT32(nalts, AximuxState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property aximux_properties[] = {
    DEFINE_PROP_UINT32("npins", AximuxState, npins, 8),
    DEFINE_PROP_UINT32("nalts", AximuxState, nalts, 4),
};

static void aximux_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aximux_realize;
    dc->vmsd = &vmstate_aximux;
    device_class_set_props(dc, aximux_properties);
}

static const TypeInfo aximux_info = {
    .name = TYPE_AXIMUX,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AximuxState),
    .class_init = aximux_class_init,
};

static void aximux_register_types(void)
{
    type_register_static(&aximux_info);
}

type_init(aximux_register_types)
