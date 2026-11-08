#!/usr/bin/env python3
"""Exercise actual SM helpers/adapter and OpenSBI dispatch with mocked switches."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(text, signature):
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    for pos in range(brace, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[start:pos + 1]
    raise ValueError(signature)


class SmPortTests(unittest.TestCase):
    def test_actual_trap_and_ecall_paths(self):
        sm = (ROOT / "keystone/sm/src/sm-sbi.c").read_text()
        adapter = (ROOT / "keystone/sm/src/sm-sbi-opensbi.c").read_text()
        core = (ROOT / "opensbi/lib/sbi/sbi_ecall.c").read_text()
        trap = (ROOT / "keystone/sm/src/sbi_trap_hack.c").read_text()
        interface = (ROOT / "opensbi/include/sbi/sbi_ecall_interface.h").read_text()
        names = {"SBI_EXT_0_1_SET_TIMER", "SBI_EXT_0_1_SHUTDOWN", "SBI_EXT_0_1_CONSOLE_GETCHAR",
                 "SBI_SUCCESS", "SBI_ERR_FAILED", "SBI_LAST_ERR", "SBI_ERR_BAD_RANGE"}
        constants = "\n".join(line for line in interface.splitlines()
                              if line.startswith("#define ") and line.split()[1] in names)
        irq = trap[trap.index("case IRQ_M_TIMER:"):trap.index("\n\t\tdefault:", trap.index("case IRQ_M_TIMER:"))]
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "sm_call.h"
#include "sm_err.h"
#define SM_TRACE_VALUES(...) ((void)0)
#define KEYSTONE_K3_PMP_N_REG 16
#define ENCL_MAX 16
#define STOP_TIMER_INTERRUPT 0
#define SBI_ENOTSUPP (-2)
#define sbi_printf(...) ((void)0)
#define IRQ_M_TIMER 7
#define IRQ_M_SOFT 3
struct sbi_trap_regs { unsigned long a0,a1,a2,a3,a6,a7,mepc,zero; };
struct sbi_trap_context { struct sbi_trap_regs regs; };
struct sbi_ecall_return { unsigned long value; bool skip_regs_update; };
struct sbi_ecall_extension { int (*handle)(unsigned long,unsigned long,struct sbi_trap_regs*,struct sbi_ecall_return*); };
static int ready=1, in_enclave=0;
static unsigned long status=0, interrupted_pc;
static int sm_is_ready(void) { return ready; }
static int cpu_is_enclave_context(void) { return in_enclave; }
static int cpu_get_enclave_id(void) { return 0; }
static unsigned long run_enclave(struct sbi_trap_regs *r,unsigned int id) {
 (void)id; if(!status){r->mepc=0x3000-4;r->a1=0x1234;} return status;
}
static unsigned long resume_enclave(struct sbi_trap_regs *r,unsigned int id) {
 (void)id; if(!status)r->mepc=0x4000-4; return status;
}
static unsigned long stop_enclave(struct sbi_trap_regs *r,unsigned long req,int id) {
 (void)req;(void)id;interrupted_pc=r->mepc;r->mepc=0x1000;return SBI_ERR_SM_ENCLAVE_INTERRUPTED;
}
static unsigned long exit_enclave(struct sbi_trap_regs *r,int id) {
 (void)id;r->mepc=0x1000;return 0;
}
static unsigned long sbi_sm_create_enclave(unsigned long *eid,uintptr_t a){(void)a;*eid=42;return status;}
static unsigned long sbi_sm_destroy_enclave(unsigned long e){(void)e;return status;}
static unsigned long sbi_sm_random(void){return 42;}
static unsigned long sbi_sm_attest_enclave(uintptr_t a,uintptr_t b,uintptr_t c){(void)a;(void)b;(void)c;return status;}
static unsigned long sbi_sm_get_sealing_key(uintptr_t a,uintptr_t b,uintptr_t c){(void)a;(void)b;(void)c;return status;}
static unsigned long sbi_sm_call_plugin(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d){(void)a;(void)b;(void)c;(void)d;return status;}
'''
        for name in ("sbi_sm_run_enclave", "sbi_sm_resume_enclave", "sbi_sm_stop_enclave", "sbi_sm_exit_enclave"):
            code += function(sm, "unsigned long " + name + "(") + "\n"
        code += function(adapter, "static int keystone_ecall_return(") + "\n"
        code += function(adapter, "static int sbi_ecall_keystone_enclave_handler(") + "\n"
        code += r'''
static struct sbi_ecall_extension ext={.handle=sbi_ecall_keystone_enclave_handler};
static struct sbi_ecall_extension *sbi_ecall_find_extension(unsigned long id){(void)id;return &ext;}
'''
        code += constants + "\n" + function(core, "int sbi_ecall_handler(") + "\n"
        code += 'static void interrupt_entry(struct sbi_trap_regs *regs, int cause) { int rc; const char *msg; switch(cause) {\n' + irq
        code += '\ndefault: abort(); } return; trap_error: (void)msg; abort(); }\n'
        code += r'''
static struct sbi_trap_context call(unsigned long id) {
 struct sbi_trap_context c={.regs={.a7=SBI_EXT_EXPERIMENTAL_KEYSTONE_ENCLAVE,.a6=id,.mepc=0x1000}};
 assert(!sbi_ecall_handler(&c));return c;
}
int main(void) {
 struct sbi_trap_context c;
 for(int irq=3;irq<=7;irq+=4) {
  struct sbi_trap_regs r={.mepc=0x9000};interrupt_entry(&r,irq);
  assert(r.mepc==0x1004);assert(interrupted_pc==0x8ffc);assert(r.a0==SBI_ERR_SM_ENCLAVE_INTERRUPTED);
  status=0;r.zero=1;assert(!sbi_sm_resume_enclave(&r,0));assert(r.mepc==0x4000);
 }
 status=0;c=call(SBI_SM_CREATE_ENCLAVE);assert(c.regs.a0==0 && c.regs.a1==42 && c.regs.mepc==0x1004);
 status=SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT;c=call(SBI_SM_CREATE_ENCLAVE);
 assert(c.regs.a0==100008 && c.regs.mepc==0x1004);
 c=call(SBI_SM_RUN_ENCLAVE);assert(c.regs.a0==100008 && c.regs.mepc==0x1004);
 status=0;c=call(SBI_SM_RUN_ENCLAVE);assert(c.regs.mepc==0x3000 && c.regs.a1==0x1234);
 ready=0;c=call(SBI_SM_CREATE_ENCLAVE);assert(c.regs.a0==SBI_ERR_SM_NOT_IMPLEMENTED && c.regs.mepc==0x1004);
 ready=1;in_enclave=1;c=call(SBI_SM_CREATE_ENCLAVE);assert(c.regs.a0==SBI_ERR_SM_ENCLAVE_SBI_PROHIBITED);
 c=call(SBI_SM_STOP_ENCLAVE);assert(c.regs.a0==SBI_ERR_SM_ENCLAVE_INTERRUPTED && c.regs.mepc==0x1004);
 c=call(SBI_SM_EXIT_ENCLAVE);assert(c.regs.a0==0 && c.regs.mepc==0x1004);
 c=call(SBI_SM_ATTEST_ENCLAVE);assert(c.regs.a0==SBI_ERR_SM_NOT_IMPLEMENTED);
 c=call(SBI_SM_GET_SEALING_KEY);assert(c.regs.a0==SBI_ERR_SM_NOT_IMPLEMENTED);
 c=call(SBI_SM_RANDOM);assert(c.regs.a0==SBI_ERR_SM_NOT_IMPLEMENTED);
 in_enclave=0;c=call(1);assert(c.regs.a0==SBI_ERR_SM_DEPRECATED);
 puts("PC recovery, context-switch frames and Keystone error ABI: passed");
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "test.c"
            binary = Path(temp) / "test"
            source.write_text(code)
            subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                            "-Wno-type-limits", "-I" + str(ROOT / "keystone/sdk/include/shared"),
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
