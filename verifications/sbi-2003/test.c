#include "../common/harness.c"
int main(void) {
 struct sbi_trap_regs r;
 reset_fixture();unsigned long id=create_fixture();
 csrs[satp]=0x9876;csrs[mideleg]=0xffff;csrs[sstatus]=0x111;
 r=invoke(2003,id);
 assert(r.a0==0 && r.a1==(uintptr_t)epm && r.a2==sizeof epm);
 assert(r.a3==args.runtime_paddr && r.a4==args.user_paddr && r.a5==args.free_paddr);
 assert(r.a6==(uintptr_t)utm && r.a7==sizeof utm && r.mepc==(uintptr_t)epm);
 assert(r.mstatus==(1UL<<MSTATUS_MPP_SHIFT));assert(csrs[satp]==0 && csrs[mideleg]==0);
 assert(enclaves[id].state==RUNNING && enclaves[id].n_thread==1);
 assert(enclaves[id].threads[0].prev_mepc==0x8000 && enclaves[id].threads[0].prev_state.ra==0xabc);
 assert(enclaves[id].threads[0].prev_csrs.satp==0x9876);
 assert(enclave_context && context_eid==(int)id && vector_calls==1 && local_pmp_calls==2);
 CASE("RUN: actual state/CSR-bank/context helpers, a1-a7, exact enclave PC, saved host state");
 enclave_context=0;r=invoke(2003,id);normal_return(r,SBI_ERR_SM_ENCLAVE_NOT_FRESH);assert(enclaves[id].n_thread==1);CASE("second RUN rejected without thread increment");
 reset_fixture();id=create_fixture();enclaves[id].state=STOPPED;r=invoke(2003,id);normal_return(r,SBI_ERR_SM_ENCLAVE_NOT_FRESH);CASE("STOPPED requires RESUME 2005, not RUN");
 reset_fixture();r=invoke(2003,0);normal_return(r,SBI_ERR_SM_ENCLAVE_NOT_FRESH);r=invoke(2003,ENCL_MAX);normal_return(r,SBI_ERR_SM_ENCLAVE_NOT_FRESH);r=invoke(2003,UINT_MAX);normal_return(r,SBI_ERR_SM_ENCLAVE_NOT_FRESH);CASE("invalid/unallocated/out-of-range IDs");
 reset_fixture();id=create_fixture();enclave_context=1;r=invoke(2003,id);normal_return(r,SBI_ERR_SM_ENCLAVE_SBI_PROHIBITED);assert(enclaves[id].state==FRESH);CASE("enclave context cannot invoke host RUN");
 reset_fixture();id=create_fixture();ready=0;r=invoke(2003,id);normal_return(r,SBI_ERR_SM_NOT_IMPLEMENTED);assert(enclaves[id].state==FRESH);CASE("unready adapter rejected before state mutation");
 reset_fixture();id=create_fixture();local_pmp_fail=-1;r=invoke(2003,id);assert(!r.a0 && enclaves[id].state==RUNNING);
 OBSERVED("current context switch ignores local PMP return value (documented finding)");
 reset_fixture();id=create_fixture();r=invoke(2003,(1UL<<32)|id);assert(!r.a0 && enclaves[id].state==RUNNING);
 OBSERVED("64-bit EID truncates to 32-bit and aliases valid enclave (documented finding)");
 printf("SUMMARY SBI 2003: %d groups completed, %d known findings reproduced (hardware mocked)\n",cases,findings);return 0;
}
