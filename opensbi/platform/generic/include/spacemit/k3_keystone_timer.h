/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef K3_KEYSTONE_TIMER_H
#define K3_KEYSTONE_TIMER_H
#include <sbi/sbi_types.h>
int k3_keystone_timer_init(bool cold_boot);
int k3_keystone_timer_enter(u64 *enclave_deadline);
void k3_keystone_timer_arm(void);
void k3_keystone_timer_exit(void);
void k3_keystone_timer_fired(void);
bool k3_keystone_timer_event(u64 next_event);
#endif
