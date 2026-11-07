#include "../common/harness.c"
_Static_assert(SBI_SM_RESUME_ENCLAVE==2005,"2004 must not be substituted for RESUME");
int main(void) {
 struct sbi_trap_regs r;
 reset_fixture();r=invoke(2004,0);normal_return(r,SBI_ERR_SM_NOT_IMPLEMENTED);
 assert(init_calls==0 && !enclave_context);no_allocated_eids();CASE("unassigned 2004 returns 100100 with PC+4 and zero a1");
 reset_fixture();unsigned long id=create_fixture();struct enclave snapshot=enclaves[id];
 r=invoke(2004,id);normal_return(r,SBI_ERR_SM_NOT_IMPLEMENTED);
 assert(!memcmp(&snapshot,&enclaves[id],sizeof snapshot) && active_regions==2);CASE("2004 does not run/resume/destroy existing enclave");
 reset_fixture();enclave_context=1;r=invoke(2004,0);normal_return(r,SBI_ERR_SM_ENCLAVE_SBI_PROHIBITED);CASE("host-range check precedes unsupported-ID handling");
 reset_fixture();ready=0;r=invoke(2004,0);normal_return(r,SBI_ERR_SM_NOT_IMPLEMENTED);CASE("readiness gate precedes unsupported-ID handling");
 reset_fixture();r=invoke(2004,ULONG_MAX);normal_return(r,SBI_ERR_SM_NOT_IMPLEMENTED);assert(init_calls==0);CASE("unused argument does not cause memory access or allocation");
 printf("SUMMARY SBI 2004: %d groups completed, %d known findings reproduced (unassigned ID)\n",cases,findings);return 0;
}
