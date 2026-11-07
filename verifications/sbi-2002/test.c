#include "../common/harness.c"
int main(void) {
 struct sbi_trap_regs r;
 reset_fixture();unsigned long id=create_fixture();memset(epm,0x71,sizeof epm);memset(utm,0x93,sizeof utm);
 op_count=0;operations[0]=0;r=invoke(2002,id);normal_return(r,0);
 assert(!strcmp(operations,"DUFF"));assert(active_regions==0 && enclaves[id].state==INVALID);
 assert(!enclaves[id].encl_satp && !enclaves[id].n_thread);
 struct runtime_params_t zero={0};assert(!memcmp(&enclaves[id].params,&zero,sizeof zero));
 for(size_t i=0;i<sizeof epm;i++)assert(epm[i]==0);
 for(size_t i=0;i<sizeof utm;i++)assert(utm[i]==0x93);
 for(int i=0;i<ENCLAVE_REGIONS_MAX;i++)assert(enclaves[id].regions[i].type==REGION_INVALID);
 CASE("destroy FRESH: DESTROYING callback, zero private pages, retain UTM, release resources");
 r=invoke(2002,id);normal_return(r,SBI_ERR_SM_ENCLAVE_NOT_DESTROYABLE);CASE("double destroy rejected");
 reset_fixture();id=create_fixture();enclaves[id].state=STOPPED;r=invoke(2002,id);normal_return(r,0);assert(active_regions==0);CASE("STOPPED enclave destroyable");
 reset_fixture();id=create_fixture();enclaves[id].state=RUNNING;r=invoke(2002,id);normal_return(r,SBI_ERR_SM_ENCLAVE_NOT_DESTROYABLE);assert(active_regions==2 && enclaves[id].state==RUNNING);CASE("RUNNING rejected without freeing resources");
 for(int state=DESTROYING;state<=ALLOCATED;state++){reset_fixture();id=create_fixture();enclaves[id].state=state;r=invoke(2002,id);normal_return(r,0);}
 OBSERVED("current state predicate also accepts DESTROYING and ALLOCATED (documented finding)");
 reset_fixture();r=invoke(2002,ENCL_MAX);normal_return(r,SBI_ERR_SM_ENCLAVE_NOT_DESTROYABLE);r=invoke(2002,UINT_MAX);normal_return(r,SBI_ERR_SM_ENCLAVE_NOT_DESTROYABLE);CASE("out-of-range IDs rejected");
 reset_fixture();id=create_fixture();enclave_context=1;r=invoke(2002,id);normal_return(r,SBI_ERR_SM_ENCLAVE_SBI_PROHIBITED);assert(active_regions==2);CASE("enclave context rejected");
 reset_fixture();ready=0;r=invoke(2002,0);normal_return(r,SBI_ERR_SM_NOT_IMPLEMENTED);CASE("unready adapter rejected");
 reset_fixture();for(int i=0;i<100;i++){op_count=0;operations[0]=0;id=create_fixture();r=invoke(2002,id);normal_return(r,0);assert(active_regions==0);}
 CASE("100 create/destroy cycles reuse EID and mocked PMP regions");
 reset_fixture();id=create_fixture();fail_unset=-1;r=invoke(2002,id);normal_return(r,0);assert(active_regions==0);
 OBSERVED("current implementation ignores PMP unset error (documented finding)");
 reset_fixture();id=create_fixture();r=invoke(2002,(1UL<<32)|id);normal_return(r,0);
 OBSERVED("64-bit EID truncates to 32-bit and aliases valid enclave (documented finding)");
 printf("SUMMARY SBI 2002: %d groups completed, %d known findings reproduced (hardware mocked)\n",cases,findings);return 0;
}
