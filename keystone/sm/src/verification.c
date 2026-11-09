/* SPDX-License-Identifier: BSD-2-Clause */
#include "verification.h"
#ifdef KEYSTONE_VERIFICATION
#include "cpu.h"
#include "sm_err.h"
#include <sbi/riscv_locks.h>
#include <sbi/sbi_ipi.h>
#include <sbi/sbi_timer.h>
#include <sbi/riscv_asm.h>
static spinlock_t control_lock = SPIN_LOCK_INITIALIZER;
static unsigned long selected_stage;
static unsigned long selected_eid;
static unsigned long duration;
/* 0=disarmed, 1=armed, 2=entered, 3=completed. Release/acquire publication. */
static unsigned long state;
static unsigned long transaction_eid[16];
static unsigned int transaction_active[16];

unsigned long sm_verify_control(unsigned long operation, unsigned long stage,
				unsigned long eid, unsigned long ticks,
				unsigned long *value)
{
    unsigned long rc;

    if (cpu_is_enclave_context()) {
	return SBI_ERR_SM_ENCLAVE_SBI_PROHIBITED;
    }
    if (operation == 1) {
	*value = __atomic_load_n(&state, __ATOMIC_ACQUIRE);
	return 0;
    }
    if ((operation != 0 && operation != 2) ||
	(operation == 0 &&
	 (stage < SM_VERIFY_RUN || stage > SM_VERIFY_DESTROY || !ticks ||
	  ticks > 25000000)) ||
	(operation == 2 && (stage < 10 || stage > 15))) {
	return SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT;
    }
    spin_lock(&control_lock);
    rc = SBI_ERR_SM_ENCLAVE_NOT_RUNNABLE;
    if (__atomic_load_n(&state, __ATOMIC_ACQUIRE) != 2) {
	selected_stage = stage;
	selected_eid = eid;
	duration = ticks;
	__atomic_store_n(&state, operation == 2 ? 4 : 1, __ATOMIC_RELEASE);
	rc = 0;
    }
    spin_unlock(&control_lock);
    return rc;
}

int sm_verify_fault(unsigned long stage, unsigned long eid)
{
    int fired;

    fired = 0;
    spin_lock(&control_lock);
    if (__atomic_load_n(&state, __ATOMIC_ACQUIRE) == 4 &&
	selected_stage == stage && selected_eid == eid) {
	__atomic_store_n(&state, 5, __ATOMIC_RELEASE);
	fired = 1;
    }
    spin_unlock(&control_lock);
    return fired;
}

void sm_verify_transaction(unsigned long eid, int active)
{
    unsigned long hart;

    hart = csr_read(CSR_MHARTID);
    if (hart < 16) {
        transaction_eid[hart] = eid;
        transaction_active[hart] = active;
    }
}

int sm_verify_ipi_fault(void)
{
    unsigned long hart;

    hart = csr_read(CSR_MHARTID);
    return hart < 16 && transaction_active[hart] &&
        sm_verify_fault(15, transaction_eid[hart]);
}

void sm_verify_gate(unsigned long stage, unsigned long eid)
{
    unsigned long ticks;
    unsigned long started;

    ticks = 0;
    spin_lock(&control_lock);
    if (__atomic_load_n(&state, __ATOMIC_ACQUIRE) == 1 &&
	selected_stage == stage && selected_eid == eid) {
	ticks = duration;
	__atomic_store_n(&state, 2, __ATOMIC_RELEASE);
    }
    spin_unlock(&control_lock);
    if (!ticks) {
	return;
    }
    /* Every call site is after encl_lock release. Never hold a production
     * lock while waiting for a Linux participant. The gate expires itself. */
    started = sbi_timer_value();
    while (sbi_timer_value() - started < ticks) {
	sbi_ipi_process();
    }
    __atomic_store_n(&state, 3, __ATOMIC_RELEASE);
}
#endif
