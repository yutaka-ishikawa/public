/*
 * Milk-V Jupiter 2 / SpacemiT K3 research machine (partial model).
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Hardware positions from the supplied running board DTB (2026-11-04).
 * It is NOT a K3 SoC emulator, not safe for hardware security validation.
 */
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/riscv/jupiter2.h"
#include "hw/core/boards.h"
#include "qemu/module.h"

/* VIRT inherited devices marked EMULATED_PLACEHOLDER are not K3 hardware. */
const MemMapEntry jupiter2_memmap[] = {
    [VIRT_DEBUG] =        { 0x0, 0x100 },
    [VIRT_MROM] =         { 0x1000, 0xf000 },        /* placeholder */
    [VIRT_TEST] =         { 0x100000, 0x1000 },     /* placeholder */
    [VIRT_RTC] =          { 0x101000, 0x1000 },     /* placeholder */
    [VIRT_CLINT] =        { 0xe081c000, 0x4000 },
    [VIRT_ACLINT_SSWI] =  { 0x2f00000, 0x4000 },   /* placeholder */
    [VIRT_PLIC] =         { 0xc000000, VIRT_PLIC_SIZE(VIRT_CPUS_MAX * 2) },
    [VIRT_APLIC_M] =      { 0xe0800000, 0x4000 },  /* unverified M-mode */
    [VIRT_APLIC_S] =      { 0xe0804000, 0x4000 },
    [VIRT_UART0] =        { 0xd4017000, 0x100 },
    [VIRT_VIRTIO] =       { 0x10001000, 0x1000 },  /* placeholder */
    [VIRT_FW_CFG] =       { 0x10100000, 0x18 },    /* placeholder */
    [VIRT_IMSIC_M] =      { 0xe0000000, 0x400000 },/* unverified M-mode */
    [VIRT_IMSIC_S] =      { 0xe0400000, 0x400000 },
    [VIRT_FLASH] =        { 0x20000000, 0x4000000 },/* placeholder NOT K3 QSPI */
    [VIRT_DRAM] =         { 0x102000000ULL, 0 },
    [VIRT_PCIE_MMIO] =    { 0x40000000, 0x40000000 }, /* placeholder */
    [VIRT_PCIE_PIO] =     { 0x3000000, 0x10000 },
    [VIRT_PLATFORM_BUS] = { 0x4000000, 0x2000000 },
    [VIRT_PCIE_ECAM] =    { 0x30000000, 0x10000000 },
    [VIRT_IOMMU_SYS] =    { 0x3010000, 0x1000 }, /* placeholder */
};

static void jupiter2_machine_instance_init(Object *obj)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);
    s->aia_type = VIRT_AIA_TYPE_APLIC_IMSIC;
    s->num_sources = JUPITER2_APLIC_SOURCES;
    s->aia_guests = 63; /* Diagnostic: restore Stage 4 layout */
}

static void jupiter2_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "Milk-V Jupiter 2 K3-derived partial machine (NOT SoC-compatible)";
    mc->default_cpus = 16;
    mc->max_cpus = 16;
}

static const TypeInfo jupiter2_machine_typeinfo = {
    .name = TYPE_RISCV_JUPITER2_MACHINE,
    .parent = TYPE_RISCV_VIRT_MACHINE,
    .class_init = jupiter2_machine_class_init,
    .instance_init = jupiter2_machine_instance_init,
};
static void jupiter2_register_types(void)
{
    type_register_static(&jupiter2_machine_typeinfo);
}
type_init(jupiter2_register_types)
