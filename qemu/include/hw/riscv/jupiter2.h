/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_RISCV_JUPITER2_H
#define HW_RISCV_JUPITER2_H
#include "hw/riscv/virt.h"
#include "qom/object.h"
#define TYPE_RISCV_JUPITER2_MACHINE MACHINE_TYPE_NAME("jupiter2")
#define JUPITER2_TIMEBASE_HZ 24000000
#define JUPITER2_DRAM_BASE 0x102000000ULL
#define JUPITER2_UART_IRQ 42
#define JUPITER2_IMSIC_IDS 511
#define JUPITER2_APLIC_SOURCES 512
extern const MemMapEntry jupiter2_memmap[];
static inline bool jupiter2_is_machine(const void *machine)
{
    return object_dynamic_cast(OBJECT(machine), TYPE_RISCV_JUPITER2_MACHINE) != NULL;
}
#endif
