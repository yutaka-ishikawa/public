/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef KEYSTONE_VERIFICATION_H
#define KEYSTONE_VERIFICATION_H
/* Private diagnostic FID, absent from production builds and shared SM ABI. */
#define SM_VERIFY_FID 4900
#define SM_VERIFY_RUN 1
#define SM_VERIFY_RESUME 2
#define SM_VERIFY_DESTROY 3
#ifdef KEYSTONE_VERIFICATION
unsigned long sm_verify_control(unsigned long operation, unsigned long stage,
				unsigned long eid, unsigned long ticks,
				unsigned long *value);
void sm_verify_transaction(unsigned long eid, int active);
int sm_verify_ipi_fault(void);
int sm_verify_fault(unsigned long stage, unsigned long eid);
void sm_verify_gate(unsigned long stage, unsigned long eid);
#else
#define sm_verify_transaction(eid, active) ((void)0)
#define sm_verify_ipi_fault() 0
#define sm_verify_fault(stage, eid) 0
#define sm_verify_gate(stage, eid) ((void)0)
#endif
#endif
