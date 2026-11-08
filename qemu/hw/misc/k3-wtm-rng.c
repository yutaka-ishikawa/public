/* SPDX-License-Identifier: GPL-2.0-or-later */
/* CPU-access subset of the K3 WTM RNG; no DMA/TE200 security claim. */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/guest-random.h"
#include "qapi/error.h"
#include "hw/core/sysbus.h"
#include "hw/misc/k3-wtm-rng.h"
#include "system/device_tree.h"
#include "migration/vmstate.h"
#include "../../../keystone/sdk/include/shared/k3_rng_regs.h"
#define TYPE_K3_WTM_RNG "k3-wtm-rng"
OBJECT_DECLARE_SIMPLE_TYPE(K3WtmRng, K3_WTM_RNG)

struct K3WtmRng {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    MemoryRegion main;
    MemoryRegion ap;
    uint32_t main_clock;
    uint32_t ap_clock;
    uint32_t ctrl;
    uint32_t trng;
    uint32_t data;
    bool valid;
    unsigned int fault;
};

static bool clock_enabled(K3WtmRng *s)
{
    return (s->main_clock & (1U << 5)) && s->ap_clock == K3_RNG_AP_CLOCK_VALUE;
}

static void refill(K3WtmRng *s)
{
    Error *err;

    s->valid = false;
    if (!clock_enabled(s) || !(s->ctrl & K3_RNG_ENABLE) || s->fault == 1) {
	return;
    }
    err = NULL;
    if (s->fault == 2) {
	s->data = 0;
	s->valid = true;
    } else if (!qemu_guest_getrandom(&s->data, sizeof s->data, &err)) {
	s->valid = true;
    }
    error_free(err);
}

static uint64_t rng_read(void *opaque, hwaddr offset, unsigned size)
{
    K3WtmRng *s;
    uint32_t value;

    s = opaque;
    switch (offset) {
    case K3_RNG_OFFSET + K3_RNG_CTRL:
	if (!s->valid) {
	    refill(s);
	}
	return s->ctrl | (s->valid ? K3_RNG_VALID : 0);
    case K3_RNG_OFFSET + K3_RNG_DATA:
	value = s->valid ? s->data : 0;
	s->valid = false;
	return value;
    case K3_RNG_OFFSET + K3_RNG_TRNG_CTRL:
	return s->trng;
    default:
	return 0;
    }
}

static void rng_write(void *opaque, hwaddr offset, uint64_t value,
		      unsigned size)
{
    K3WtmRng *s;

    s = opaque;
    switch (offset) {
    case K3_RNG_OFFSET + K3_RNG_CTRL:
	s->ctrl = value & K3_RNG_ENABLE;
	refill(s);
	break;
    case K3_RNG_OFFSET + K3_RNG_FIFO:
	if (value & K3_RNG_FIFO_CLEAR) {
	    refill(s);
	}
	break;
    case K3_RNG_OFFSET + K3_RNG_TRNG_CTRL:
	s->trng = value;
	break;
    default:
	break;
    }
}

static uint64_t main_read(void *opaque, hwaddr offset, unsigned size)
{
    K3WtmRng *s;

    s = opaque;
    return offset == K3_RNG_MAIN_CLOCK ? s->main_clock : 0;
}

static void main_write(void *opaque, hwaddr offset, uint64_t value,
		       unsigned size)
{
    K3WtmRng *s;

    s = opaque;
    if (offset == K3_RNG_MAIN_CLOCK) {
	s->main_clock = value;
	s->valid = false;
    }
}

static uint64_t ap_read(void *opaque, hwaddr offset, unsigned size)
{
    K3WtmRng *s;

    s = opaque;
    return offset == K3_RNG_AP_CLOCK ? s->ap_clock : 0;
}

static void ap_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    K3WtmRng *s;

    s = opaque;
    if (offset == K3_RNG_AP_CLOCK) {
	s->ap_clock = value;
	s->valid = false;
    }
}

#define RNG_OPS(read_fn, write_fn)                                             \
    {.read = read_fn,                                                          \
     .write = write_fn,                                                        \
     .endianness = DEVICE_LITTLE_ENDIAN,                                       \
     .valid.min_access_size = 4,                                               \
     .valid.max_access_size = 4}
static const MemoryRegionOps rng_ops = RNG_OPS(rng_read, rng_write);
static const MemoryRegionOps main_ops = RNG_OPS(main_read, main_write);
static const MemoryRegionOps ap_ops = RNG_OPS(ap_read, ap_write);

static void rng_reset(DeviceState *device)
{
    K3WtmRng *s;

    s = K3_WTM_RNG(device);
    s->main_clock = s->ap_clock = s->ctrl = s->trng = s->data = 0;
    s->valid = false;
}

static void rng_init(Object *obj)
{
    K3WtmRng *s;

    s = K3_WTM_RNG(obj);
    memory_region_init_io(&s->regs, obj, &rng_ops, s, "k3-wtm-rng",
			  K3_RNG_MAP_SIZE);
    memory_region_init_io(&s->main, obj, &main_ops, s, "k3-rng-main-clock",
			  K3_RNG_MAIN_SIZE);
    memory_region_init_io(&s->ap, obj, &ap_ops, s, "k3-rng-ap-clock",
			  K3_RNG_AP_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->main);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->ap);
}

static const VMStateDescription rng_vmstate = {
    .name = TYPE_K3_WTM_RNG,
    .unmigratable = true,
};

static void rng_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc;

    dc = DEVICE_CLASS(klass);
    device_class_set_legacy_reset(dc, rng_reset);
    /* There is no safe migration contract for the RNG state yet. */
    dc->vmsd = &rng_vmstate;
}

static const TypeInfo rng_type = {
    .name = TYPE_K3_WTM_RNG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(K3WtmRng),
    .instance_init = rng_init,
    .class_init = rng_class_init,
};

static void rng_register_types(void)
{
    type_register_static(&rng_type);
}

type_init(rng_register_types)

    void k3_wtm_rng_create(MachineState *machine, const char *fault)
{
    DeviceState *dev;
    K3WtmRng *s;
    const char *node;

    dev = qdev_new(TYPE_K3_WTM_RNG);
    s = K3_WTM_RNG(dev);
    if (!strcmp(fault, "not-ready")) {
	s->fault = 1;
    } else if (!strcmp(fault, "zero")) {
	s->fault = 2;
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, K3_RNG_MAP_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, K3_RNG_MAIN_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, K3_RNG_AP_BASE);
    node = "/soc/rng@f0703000";
    qemu_fdt_add_subnode(machine->fdt, node);
    qemu_fdt_setprop_string(machine->fdt, node, "compatible",
			    "spacemit,k3-wtm-rng");
    qemu_fdt_setprop_cells(machine->fdt, node, "reg", 0, K3_RNG_MAP_BASE, 0,
			   K3_RNG_MAP_SIZE);
    qemu_fdt_setprop_string(machine->fdt, node, "status", "okay");
}
