//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include "mm/mm.h"
#include "util/rt_util.h"
#include "util/printf.h"
#include "uaccess.h"
#include "mm/vm.h"
#include "sm_err.h"

// Statically allocated copy-buffer
unsigned char rt_copy_buffer_1[RISCV_PAGE_SIZE];
unsigned char rt_copy_buffer_2[RISCV_PAGE_SIZE];

size_t rt_util_getrandom(void *vaddr, size_t buflen)
{
    size_t done;
    size_t count;
    uintptr_t random;
    unsigned long status;
    long error;

    done = 0;
    while (done < buflen) {
	status = sbi_random_checked(&random);
	if (status) {
	    if (status == SBI_ERR_SM_NOT_IMPLEMENTED) {
		error = -38;
	    } else if (status == SBI_ERR_SM_RNG_NOT_READY) {
		error = -11;
	    } else {
		error = -5;
	    }
	    return done ? done : (size_t)error;
	}
	count = buflen - done;
	if (count > sizeof random) {
	    count = sizeof random;
	}
	if (copy_to_user((unsigned char *)vaddr + done, &random, count)) {
	    return done ? done : (size_t)-14;
	}
	done += count;
    }
    return done;
}

void rt_util_misc_fatal(){
  //Better hope we can debug it!
  sbi_exit_enclave(-1);
}

void not_implemented_fatal(struct encl_ctx* ctx){
#ifdef FATAL_DEBUG
    unsigned long addr, cause, pc;
    pc = ctx->regs.sepc;
    addr = ctx->sbadaddr;
    cause = ctx->scause;
    printf("[runtime] non-handlable interrupt/exception at 0x%lx on 0x%lx (scause: 0x%lx)\r\n", pc, addr, cause);
#endif

    // Bail to m-mode
    __asm__ volatile("csrr a0, scause\r\nli a7, 1111\r\n ecall");

    return;
}

void rt_page_fault(struct encl_ctx* ctx)
{
#ifdef FATAL_DEBUG
  unsigned long addr, cause, pc;
  pc = ctx->regs.sepc;
  addr = ctx->sbadaddr;
  cause = ctx->scause;
  printf("[runtime] page fault at 0x%lx on 0x%lx (scause: 0x%lx)\r\n", pc, addr, cause);
#endif

  sbi_exit_enclave(-1);

  /* never reach here */
  assert(false);
  return;
}

void tlb_flush(void)
{
  __asm__ volatile("fence.i\t\nsfence.vma\t\n");
}
