/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef __PMP_IPI_H__
#define __PMP_IPI_H__
#include <sbi/sbi_types.h>
#define SBI_PMP_IPI_TYPE_SET 0
#define SBI_PMP_IPI_TYPE_UNSET 1
/* Return 0 only after every required hart has completed the PMP update. */
int k3_pmp_sync_initialize(void);
int k3_pmp_sync_healthy(void);
int k3_keystone_allow_initial_hsm_starts(void);
int k3_keystone_pmp_assert_active(void);
int k3_keystone_pmp_transition_begin(void);
void k3_keystone_pmp_transition_finish(void);
int send_and_sync_pmp_ipi(int region_idx, int type, uint8_t perm);
#endif
