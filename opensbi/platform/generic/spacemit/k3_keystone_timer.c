/* SPDX-License-Identifier: BSD-2-Clause */
/* RV64: Linux retains Sstc; the SM owns MTIMER while an enclave runs. */
#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_timer.h>
#include <spacemit/k3_keystone_timer.h>

#if __riscv_xlen != 64
#error "K3 Keystone timer coexistence currently supports RV64 only"
#endif

#define TIMER_NEVER (~(u64)0)
#define SLICE_HZ 100

struct k3_timer_state {
    u64 *enclave_deadline;
    u64 host_deadline;
    u64 host_stimecmp;
    u64 slice_deadline;
    unsigned long host_stce;
    unsigned long host_mtie;
    unsigned long host_stip;
    bool sstc;
};

static unsigned long timer_offset;

static struct k3_timer_state *this_timer(void)
{
    if (!timer_offset) {
	return NULL;
    }
    return sbi_scratch_offset_ptr(sbi_scratch_thishart_ptr(), timer_offset);
}

int k3_keystone_timer_init(bool cold_boot)
{
    struct k3_timer_state *state;

    if (cold_boot) {
	timer_offset = sbi_scratch_alloc_offset(sizeof *state);
	if (!timer_offset) {
	    return SBI_ENOMEM;
	}
    }
    state = this_timer();
    if (!state) {
	return SBI_ENOMEM;
    }
    state->host_deadline = TIMER_NEVER;
    state->enclave_deadline = NULL;
    state->sstc = sbi_hart_has_extension(sbi_scratch_thishart_ptr(),
				       SBI_HART_EXT_SSTC);
    if (cold_boot) {
	sbi_printf("[SM/TIMER] host=%s enclave=mtimer slice-us=10000\n",
		   state->sstc ? "sstc" : "sbi-mtimer");
    }
    return 0;
}

static void program_enclave_timer(struct k3_timer_state *state)
{
    const struct sbi_timer_device *device;
    u64 deadline;

    device = sbi_timer_get_device();
    deadline = state->slice_deadline;
    if (*state->enclave_deadline < deadline) {
	deadline = *state->enclave_deadline;
    }
    /* Use the existing MTIMER driver, never the host Sstc selection path. */
    device->timer_event_start(deadline);
    csr_clear(CSR_MIP, MIP_STIP);
    csr_set(CSR_MIE, MIP_MTIP);
}

int k3_keystone_timer_enter(u64 *enclave_deadline)
{
    const struct sbi_timer_device *device;
    struct k3_timer_state *state;

    device = sbi_timer_get_device();
    state = this_timer();
    if (!state || state->enclave_deadline || !enclave_deadline || !device ||
	!device->timer_event_start || !device->timer_event_stop ||
	!device->timer_freq) {
	return SBI_ENOTSUPP;
    }
    state->host_mtie = csr_read(CSR_MIE) & MIP_MTIP;
    state->host_stip = csr_read(CSR_MIP) & MIP_STIP;
    csr_clear(CSR_MIE, MIP_MTIP);
    device->timer_event_stop();
    if (state->sstc) {
	state->host_stce = csr_read(CSR_MENVCFG) & ENVCFG_STCE;
	state->host_stimecmp = csr_read(CSR_STIMECMP);
	csr_write(CSR_STIMECMP, TIMER_NEVER);
	csr_clear(CSR_MENVCFG, ENVCFG_STCE);
    }
    csr_clear(CSR_MIP, MIP_STIP);
    state->enclave_deadline = enclave_deadline;
    return 0;
}

void k3_keystone_timer_arm(void)
{
    const struct sbi_timer_device *device;
    struct k3_timer_state *state;
    u64 now;
    u64 delta;

    state = this_timer();
    device = sbi_timer_get_device();
    now = sbi_timer_value();
    delta = device->timer_freq / SLICE_HZ;
    if (!delta) {
	delta = 1;
    }
    state->slice_deadline = now > TIMER_NEVER - delta ? TIMER_NEVER :
						     now + delta;
    program_enclave_timer(state);
}

bool k3_keystone_timer_event(u64 next_event)
{
    struct k3_timer_state *state;

    state = this_timer();
    if (!state) {
	return false;
    }
    if (!state->enclave_deadline) {
	state->host_deadline = next_event;
	return false;
    }
    *state->enclave_deadline = next_event;
    program_enclave_timer(state);
    return true;
}

void k3_keystone_timer_fired(void)
{
    struct k3_timer_state *state;

    state = this_timer();
    if (state && state->enclave_deadline) {
	if (*state->enclave_deadline <= sbi_timer_value()) {
	    *state->enclave_deadline = TIMER_NEVER;
	}
	/* Reuse OpenSBI's interrupt acknowledgement; exit restores host state. */
	sbi_timer_process();
    }
}

void k3_keystone_timer_exit(void)
{
    const struct sbi_timer_device *device;
    struct k3_timer_state *state;

    state = this_timer();
    device = sbi_timer_get_device();
    csr_clear(CSR_MIE, MIP_MTIP);
    device->timer_event_stop();
    csr_clear(CSR_MIP, MIP_STIP);
    state->enclave_deadline = NULL;
    if (state->sstc) {
	csr_write(CSR_STIMECMP, state->host_stimecmp);
	if (state->host_stce) {
	    csr_set(CSR_MENVCFG, ENVCFG_STCE);
	}
    } else {
	device->timer_event_start(state->host_deadline);
	if (state->host_stip) {
	    csr_set(CSR_MIP, MIP_STIP);
	}
    }
    if (state->host_mtie) {
	csr_set(CSR_MIE, MIP_MTIP);
    }
}
