#include "sm_trace.h"
#include <sbi/sbi_trap.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_tlb.h>
#include <sbi/sbi_ipi.h>
#include <sbi/sbi_string.h>
#include <sbi/riscv_locks.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_scratch.h>
#include <sbi/riscv_asm.h>
#include <sbi/sbi_ecall.h>
#include "sm-sbi-opensbi.h"
#include "pmp.h"
#include "sm-sbi.h"
#include "sm.h"
#include "cpu.h"

/* Keystone's legacy ABI uses positive status codes. New OpenSBI accepts only
 * standard SBI errors from handle(), so complete the frame explicitly and ask
 * the common dispatcher to leave it alone. Context switches already do this. */
static int keystone_ecall_return(struct sbi_trap_regs *regs,
				 struct sbi_ecall_return *out, uintptr_t status)
{
    SM_TRACE_VALUES("sbi.return", regs->a6, status);
    regs->a0 = status;
    regs->a1 = out->value;
    regs->mepc += 4;
    out->skip_regs_update = true;
    return 0;
}

static int sbi_ecall_keystone_enclave_handler(unsigned long extid,
					      unsigned long funcid,
					      struct sbi_trap_regs *regs,
					      struct sbi_ecall_return *out)
{
    uintptr_t retval;
    out->value = 0;
    SM_TRACE_VALUES("sbi.enter", funcid, regs->mepc);

#ifdef KEYSTONE_K3_PMP_N_REG
    if (!sm_is_ready()) {
	sbi_printf("[SM/K3] Keystone SBI function %lu unavailable: SM not "
		   "initialized\n",
		   funcid);
	return keystone_ecall_return(regs, out, SBI_ERR_SM_NOT_IMPLEMENTED);
    }
    if (funcid == SBI_SM_ATTEST_ENCLAVE || funcid == SBI_SM_GET_SEALING_KEY) {
	sbi_printf("[SM/K3] secure key provider not implemented\n");
	return keystone_ecall_return(regs, out, SBI_ERR_SM_NOT_IMPLEMENTED);
    }
#endif
    if (funcid <= FID_RANGE_DEPRECATED) {
	return keystone_ecall_return(regs, out, SBI_ERR_SM_DEPRECATED);
    } else if (funcid <= FID_RANGE_HOST) {
	if (cpu_is_enclave_context()) {
	    return keystone_ecall_return(regs, out,
					 SBI_ERR_SM_ENCLAVE_SBI_PROHIBITED);
	}
    } else if (funcid <= FID_RANGE_ENCLAVE) {
	if (!cpu_is_enclave_context()) {
	    return keystone_ecall_return(regs, out,
					 SBI_ERR_SM_ENCLAVE_SBI_PROHIBITED);
	}
    }

    switch (funcid) {
    case SBI_SM_CREATE_ENCLAVE:
	retval = sbi_sm_create_enclave(&out->value, regs->a0);
	break;
    case SBI_SM_DESTROY_ENCLAVE:
	retval = sbi_sm_destroy_enclave(regs->a0);
	break;
    case SBI_SM_RUN_ENCLAVE:
	retval = sbi_sm_run_enclave((struct sbi_trap_regs *)regs, regs->a0);
	/* The helper updated the trap frame for OpenSBI fw_base.S mret.
	 * Do not let sbi_ecall_handler overwrite mepc/a0/a1. */
	out->skip_regs_update = (retval == 0);
	break;
    case SBI_SM_RESUME_ENCLAVE:
	retval = sbi_sm_resume_enclave((struct sbi_trap_regs *)regs, regs->a0);
	/* The helper updated the trap frame for OpenSBI fw_base.S mret.
	 * Do not let sbi_ecall_handler overwrite mepc/a0/a1. */
	out->skip_regs_update = (retval == 0);
	break;
    case SBI_SM_RANDOM:
	retval = sbi_sm_random(&out->value);
	break;
    case SBI_SM_ATTEST_ENCLAVE:
	retval = sbi_sm_attest_enclave(regs->a0, regs->a1, regs->a2);
	break;
    case SBI_SM_GET_SEALING_KEY:
	retval = sbi_sm_get_sealing_key(regs->a0, regs->a1, regs->a2);
	break;
    case SBI_SM_STOP_ENCLAVE:
	retval = sbi_sm_stop_enclave((struct sbi_trap_regs *)regs, regs->a0);
	/* The helper updated the trap frame for OpenSBI fw_base.S mret.
	 * Do not let sbi_ecall_handler overwrite mepc/a0/a1. */
	out->skip_regs_update = (retval == 0);
	break;
    case SBI_SM_EXIT_ENCLAVE:
	retval = sbi_sm_exit_enclave((struct sbi_trap_regs *)regs, regs->a0);
	/* The helper updated the trap frame for OpenSBI fw_base.S mret.
	 * Do not let sbi_ecall_handler overwrite mepc/a0/a1. */
	out->skip_regs_update = (retval == 0);
	break;
    case SBI_SM_CALL_PLUGIN:
	retval = sbi_sm_call_plugin(regs->a0, regs->a1, regs->a2, regs->a3);
	break;
    default:
	retval = SBI_ERR_SM_NOT_IMPLEMENTED;
	break;
    }

    /* RUN/RESUME/STOP/EXIT already selected a different frame. */
    if (out->skip_regs_update) {
	SM_TRACE_VALUES("sbi.switch-return", funcid, regs->mepc);
	return 0;
    }
    return keystone_ecall_return(regs, out, retval);
}

struct sbi_ecall_extension ecall_keystone_enclave = {
    .name = "keystone",
    .experimental = true,
    .extid_start = SBI_EXT_EXPERIMENTAL_KEYSTONE_ENCLAVE,
    .extid_end = SBI_EXT_EXPERIMENTAL_KEYSTONE_ENCLAVE,
    .handle = sbi_ecall_keystone_enclave_handler,
};
