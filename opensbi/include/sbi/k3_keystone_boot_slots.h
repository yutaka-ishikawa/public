/* SPDX-License-Identifier: BSD-2-Clause */
/* Jupiter2 QEMU-only deterministic 16-PMP boot slot layout.
 * Every root-domain entry (denies and grants) precedes Keystone free slots;
 * the root-domain universal S/U allow is always index 15.
 * This is a slot plan, not a substitute for hardware CSR readback.
 */
#ifndef __K3_KEYSTONE_BOOT_SLOTS_H__
#define __K3_KEYSTONE_BOOT_SLOTS_H__
#include <stdint.h>
#define K3_BOOT_PMP_COUNT 16U
#define K3_BOOT_FALLBACK 15U
#define K3_BOOT_MIN_KEYSTONE 3U
struct k3_boot_pmp_plan {
 uint16_t opensbi_mask;
 uint16_t keystone_mask;
 uint8_t firmware_entries;
 uint8_t fallback_index;
};
static inline int k3_boot_plan_slots(unsigned int pmp_count,
 unsigned int firmware_entries, unsigned int broad_allow_entries,
 struct k3_boot_pmp_plan *out)
{
 unsigned int i;
 uint16_t reserved = (uint16_t)(1U << K3_BOOT_FALLBACK);
 if (!out || pmp_count != K3_BOOT_PMP_COUNT ||
     !firmware_entries || broad_allow_entries != 1U ||
     firmware_entries + K3_BOOT_MIN_KEYSTONE > K3_BOOT_FALLBACK)
  return -1;
 for (i = 0; i < firmware_entries; i++) reserved |= (uint16_t)(1U << i);
 out->firmware_entries = (uint8_t)firmware_entries;
 out->fallback_index = K3_BOOT_FALLBACK;
 out->opensbi_mask = reserved;
 out->keystone_mask = (uint16_t)(0xffffU & ~reserved);
 return 0;
}
#endif
