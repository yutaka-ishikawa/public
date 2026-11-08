#include "security/security.h"
#include "sm_trace.h"
//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include "sm-sbi.h"
#include "sm.h"
#include "pmp.h"
#include "enclave.h"
#include "page.h"
#include "cpu.h"
#include "platform-hook.h"
#include "plugins/plugins.h"
#include <sbi/riscv_asm.h>
#include <sbi/sbi_console.h>

unsigned long sbi_sm_create_enclave(unsigned long* eid, uintptr_t create_args)
{
  struct keystone_sbi_create_t create_args_local;
  unsigned long ret;
#ifdef KEYSTONE_K3_PMP_N_REG
  if (!sm_is_ready()) {
    sbi_printf("[SM/K3] CREATE_ENCLAVE unavailable: PMP/SM not initialized\n");
    return SBI_ERR_SM_ENCLAVE_NOT_INITIALIZED;
  }
#endif
  SM_TRACE_STAGE("CREATE args-copy begin");
  ret = copy_enclave_create_args(create_args, &create_args_local);
  SM_TRACE_RESULT("CREATE args-copy end", ret);

  if (ret)
    return ret;

  ret = create_enclave(eid, create_args_local);
  return ret;
}

unsigned long sbi_sm_destroy_enclave(unsigned long eid)
{
  if (eid >= ENCL_MAX)
    return SBI_ERR_SM_ENCLAVE_INVALID_ID;
  unsigned long ret;
  ret = destroy_enclave((unsigned int)eid);
  return ret;
}

unsigned long sbi_sm_run_enclave(struct sbi_trap_regs *regs, unsigned long eid)
{
  if (eid >= ENCL_MAX)
    return SBI_ERR_SM_ENCLAVE_INVALID_ID;
  unsigned long rc = run_enclave(regs, (unsigned int) eid);
  if (rc == SBI_ERR_SM_ENCLAVE_SUCCESS) {
    regs->a0 = rc;
    regs->mepc += 4;
  }
  /* K3 OpenSBI restores regs and performs mret after the ecall handler. */
  return rc == SBI_ERR_SM_ENCLAVE_SUCCESS ? 0 : rc;
}

unsigned long sbi_sm_resume_enclave(struct sbi_trap_regs *regs, unsigned long eid)
{
  if (eid >= ENCL_MAX)
    return SBI_ERR_SM_ENCLAVE_INVALID_ID;
  unsigned long ret;
  ret = resume_enclave(regs, (unsigned int) eid);
  if (ret == SBI_ERR_SM_ENCLAVE_SUCCESS) {
    if (!regs->zero) regs->a0 = ret;
    regs->mepc += 4;
  }

  /* K3 OpenSBI restores regs and performs mret after the ecall handler. */
  return ret == SBI_ERR_SM_ENCLAVE_SUCCESS ? 0 : ret;
}

unsigned long sbi_sm_exit_enclave(struct sbi_trap_regs *regs, unsigned long retval)
{
  unsigned long rc = exit_enclave(regs, cpu_get_enclave_id());
  if (rc == SBI_ERR_SM_ENCLAVE_SUCCESS) {
    regs->a0 = rc;
    regs->a1 = retval;
    regs->mepc += 4;
  }
  /* K3 OpenSBI restores regs and performs mret after the ecall handler. */
  return rc == SBI_ERR_SM_ENCLAVE_SUCCESS ? 0 : rc;
}

unsigned long sbi_sm_stop_enclave(struct sbi_trap_regs *regs,
				  unsigned long request)
{
    unsigned long rc;

    rc = stop_enclave(regs, request, cpu_get_enclave_id());
    if (rc != SBI_ERR_SM_ENCLAVE_INTERRUPTED &&
	rc != SBI_ERR_SM_ENCLAVE_EDGE_CALL_HOST) {
	return rc;
    }
    /* This is now the host RUN/RESUME frame, not the enclave ecall frame. */
    regs->a0 = rc;
    regs->a1 = 0;
    regs->mepc += 4;
    return 0;
}

unsigned long sbi_sm_attest_enclave(uintptr_t report, uintptr_t data, uintptr_t size)
{
  unsigned long ret;
#ifdef KEYSTONE_K3_PMP_N_REG
  return keystone_security_call(SBI_SM_ATTEST_ENCLAVE, report, data, size);
#endif
  ret = attest_enclave(report, data, size, cpu_get_enclave_id());
  return ret;
}

unsigned long sbi_sm_get_sealing_key(uintptr_t sealing_key, uintptr_t key_ident,
                       size_t key_ident_size)
{
  unsigned long ret;
#ifdef KEYSTONE_K3_PMP_N_REG
  return keystone_security_call(SBI_SM_GET_SEALING_KEY, sealing_key, key_ident,
				key_ident_size);
#endif
  ret = get_sealing_key(sealing_key, key_ident, key_ident_size,
                         cpu_get_enclave_id());
  return ret;
}

unsigned long sbi_sm_random(unsigned long *value)
{
#ifdef KEYSTONE_K3_PMP_N_REG
    uint64_t random;
    int rc;

    rc = k3_platform_random_checked(&random);
    *value = rc ? 0 : random;
    return rc;
#else
    *value = platform_random();
    return 0;
#endif
}

unsigned long sbi_sm_call_plugin(uintptr_t plugin_id, uintptr_t call_id, uintptr_t arg0, uintptr_t arg1)
{
  unsigned long ret;
  ret = call_plugin(cpu_get_enclave_id(), plugin_id, call_id, arg0, arg1);
  return ret;
}
