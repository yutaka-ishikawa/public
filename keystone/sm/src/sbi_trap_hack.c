#include "enclave.h"
#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_ecall.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_illegal_insn.h>
#include <sbi/sbi_ipi.h>
/* K3 OpenSBI exports the load/store trap API from sbi_trap_ldst.h.
 * NOTE: the handler signatures also changed to sbi_trap_context *;
 * this include fix intentionally does NOT claim runtime compatibility. */
#include <sbi/sbi_trap_ldst.h>
#include <sbi/sbi_timer.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_trap.h>

static void sbi_trap_error(const char *msg, int rc,
				      ulong mcause, ulong mtval, ulong mtval2,
				      ulong mtinst, struct sbi_trap_regs *regs)
{
	u32 hartid = current_hartid();

	sbi_printf("%s: hart%d: %s (error %d)\n", __func__, hartid, msg, rc);
	sbi_printf("%s: hart%d: mcause=0x%" PRILX " mtval=0x%" PRILX "\n",
		   __func__, hartid, mcause, mtval);
	if (misa_extension('H')) {
		sbi_printf("%s: hart%d: mtval2=0x%" PRILX
			   " mtinst=0x%" PRILX "\n",
			   __func__, hartid, mtval2, mtinst);
	}
	sbi_printf("%s: hart%d: mepc=0x%" PRILX " mstatus=0x%" PRILX "\n",
		   __func__, hartid, regs->mepc, regs->mstatus);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "ra", regs->ra, "sp", regs->sp);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "gp", regs->gp, "tp", regs->tp);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "s0", regs->s0, "s1", regs->s1);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "a0", regs->a0, "a1", regs->a1);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "a2", regs->a2, "a3", regs->a3);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "a4", regs->a4, "a5", regs->a5);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "a6", regs->a6, "a7", regs->a7);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "s2", regs->s2, "s3", regs->s3);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "s4", regs->s4, "s5", regs->s5);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "s6", regs->s6, "s7", regs->s7);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "s8", regs->s8, "s9", regs->s9);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "s10", regs->s10, "s11", regs->s11);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "t0", regs->t0, "t1", regs->t1);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "t2", regs->t2, "t3", regs->t3);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "t4", regs->t4, "t5", regs->t5);
	sbi_printf("%s: hart%d: %s=0x%" PRILX "\n", __func__, hartid, "t6",
		   regs->t6);

  sbi_sm_exit_enclave(regs, rc);
}


/**
 * Handle trap/interrupt
 *
 * This function is called by firmware linked to OpenSBI
 * library for handling trap/interrupt. It expects the
 * following:
 * 1. The 'mscratch' CSR is pointing to sbi_scratch of current HART
 * 2. The 'mcause' CSR is having exception/interrupt cause
 * 3. The 'mtval' CSR is having additional trap information
 * 4. The 'mtval2' CSR is having additional trap information
 * 5. The 'mtinst' CSR is having decoded trap instruction
 * 6. Stack pointer (SP) is setup for current HART
 * 7. Interrupts are disabled in MSTATUS CSR
 *
 * @param regs pointer to register state
 */
void sbi_trap_handler_keystone_enclave(struct sbi_trap_regs *regs)
{
	int rc = SBI_ENOTSUPP;
	const char *msg = "trap handler failed";
	ulong mcause = csr_read(CSR_MCAUSE);
	ulong mtval = csr_read(CSR_MTVAL), mtval2 = 0, mtinst = 0;
	/* K3 handlers require a complete trap context rather than bare regs.
	 * The legacy Keystone trap.S still saves a regs-only frame. This
	 * compatibility bridge constructs a context for C dispatch and copies
	 * register changes back; nested-trap behaviour requires validation. */
	struct sbi_trap_context context = { 0 };
	struct sbi_scratch *scratch = sbi_scratch_thishart_ptr();
	struct sbi_trap_context *previous;

	if (misa_extension('H')) {
		mtval2 = csr_read(CSR_MTVAL2);
		mtinst = csr_read(CSR_MTINST);
	}

	context.regs = *regs;
	context.trap.cause = mcause;
	context.trap.tval = mtval;
	context.trap.tval2 = mtval2;
	context.trap.tinst = mtinst;
	context.trap.gva = sbi_regs_gva(regs);
	previous = sbi_trap_get_context(scratch);
	context.prev_context = previous;

	if (mcause & (1UL << (__riscv_xlen - 1))) {
		mcause &= ~(1UL << (__riscv_xlen - 1));
		switch (mcause) {
		case IRQ_M_TIMER:
		case IRQ_M_SOFT:
      /* RESUME adds four to the saved enclave PC, hence this bias only
       * applies to the interrupted enclave frame. STOP itself advances the
       * restored host ECALL PC exactly once. Unlike old sbi_trap_exit(), the
       * ported helper returns here: do not advance the host PC a second time. */
      regs->mepc -= 4;
      rc = sbi_sm_stop_enclave(regs, STOP_TIMER_INTERRUPT);
      if (rc) {
        msg = "enclave interrupt stop failed";
        goto trap_error;
      }
      break;
		default:
			msg = "unhandled external interrupt";
			goto trap_error;
		};
		return;
	}

	sbi_trap_set_context(scratch, &context);
	switch (mcause) {
	case CAUSE_ILLEGAL_INSTRUCTION:
		rc  = sbi_illegal_insn_handler(&context);
		msg = "illegal instruction handler failed";
		break;
	case CAUSE_MISALIGNED_LOAD:
		rc = sbi_misaligned_load_handler(&context);
		msg = "misaligned load handler failed";
		break;
	case CAUSE_MISALIGNED_STORE:
		rc  = sbi_misaligned_store_handler(&context);
		msg = "misaligned store handler failed";
		break;
	case CAUSE_SUPERVISOR_ECALL:
	case CAUSE_MACHINE_ECALL:
		rc  = sbi_ecall_handler(&context);
		msg = "ecall handler failed";
		break;
	default:
		/* If the trap came from S or U mode, redirect it there */
		rc = sbi_trap_redirect(&context.regs, &context.trap);
		break;
	};

trap_error:
	/* Preserve changes made by K3 handlers in the trap.S regs frame. */
	if (!(mcause & (1UL << (__riscv_xlen - 1)))) {
		*regs = context.regs;
		sbi_trap_set_context(scratch, previous);
	}
	if (rc)
		sbi_trap_error(msg, rc, mcause, mtval, mtval2, mtinst, regs);
}
