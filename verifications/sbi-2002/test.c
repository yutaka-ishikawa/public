#include "../common/harness.c"
static _Alignas(4096) unsigned char other_page[4096];
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
 CASE("destroy FRESH: zero private pages, retain UTM, free after global unset");
 r=invoke(2002,id);normal_return(r,SBI_ERR_SM_ENCLAVE_NOT_DESTROYABLE);CASE("double destroy rejected");
 reset_fixture();id=create_fixture();enclaves[id].state=STOPPED;r=invoke(2002,id);normal_return(r,0);assert(active_regions==0);CASE("STOPPED enclave destroyable");
 reset_fixture();id=create_fixture();enclaves[id].state=RUNNING;r=invoke(2002,id);normal_return(r,SBI_ERR_SM_ENCLAVE_NOT_DESTROYABLE);assert(active_regions==2 && enclaves[id].state==RUNNING);CASE("RUNNING rejected without freeing resources");
 for(int state=DESTROYING;state<=ALLOCATED;state++){
  reset_fixture();id=create_fixture();enclaves[id].state=state;r=invoke(2002,id);
  normal_return(r,SBI_ERR_SM_ENCLAVE_NOT_DESTROYABLE);assert(active_regions==2 && free_calls==0 && destroy_calls==0);
 }
 CASE("DESTROYING and ALLOCATED rejected without callbacks or frees");
 reset_fixture();r=invoke(2002,ENCL_MAX);normal_return(r,SBI_ERR_SM_ENCLAVE_INVALID_ID);
 r=invoke(2002,UINT_MAX);normal_return(r,SBI_ERR_SM_ENCLAVE_INVALID_ID);CASE("out-of-range IDs rejected before narrowing");
 reset_fixture();id=create_fixture();enclave_context=1;r=invoke(2002,id);normal_return(r,SBI_ERR_SM_ENCLAVE_SBI_PROHIBITED);assert(active_regions==2);CASE("enclave context rejected");
 reset_fixture();ready=0;r=invoke(2002,0);normal_return(r,SBI_ERR_SM_NOT_IMPLEMENTED);CASE("unready adapter rejected");
 reset_fixture();for(int i=0;i<100;i++){op_count=0;operations[0]=0;id=create_fixture();r=invoke(2002,id);normal_return(r,0);assert(active_regions==0);}
 CASE("100 create/destroy cycles reuse EID and mocked PMP regions");
 reset_fixture();id=create_fixture();fail_unset=-1;r=invoke(2002,id);normal_return(r,SBI_ERR_SM_ENCLAVE_PMP_FAILURE);
 assert(active_regions==2 && free_calls==0 && enclaves[id].state==DESTROYING);
 fail_unset=0;r=invoke(2002,id);normal_return(r,SBI_ERR_SM_ENCLAVE_NOT_DESTROYABLE);assert(active_regions==2 && free_calls==0);
 CASE("failed global unset quarantines EID and all allocations; retry rejected");
 reset_fixture();id=create_fixture();r=invoke(2002,(1UL<<32)|id);normal_return(r,SBI_ERR_SM_ENCLAVE_INVALID_ID);
 assert(enclaves[id].state==FRESH && active_regions==2 && destroy_calls==0);
 r=invoke(2002,ULONG_MAX);normal_return(r,SBI_ERR_SM_ENCLAVE_INVALID_ID);CASE("64-bit EIDs cannot alias a valid enclave");
 reset_fixture();id=create_fixture();nested_destroy=1;r=invoke(2002,id);normal_return(r,0);
 assert(nested_result==SBI_ERR_SM_ENCLAVE_NOT_DESTROYABLE && destroy_calls==1 && free_calls==2);
 CASE("reentrant destroy during callback cannot acquire DESTROYING enclave");
 reset_fixture();id=create_fixture();free_fail_call=1;r=invoke(2002,id);normal_return(r,SBI_ERR_SM_ENCLAVE_PMP_FAILURE);
 assert(enclaves[id].state==DESTROYING && active_regions==2);CASE("private descriptor free failure does not release EID or UTM");
 reset_fixture();id=create_fixture();free_fail_call=2;r=invoke(2002,id);normal_return(r,SBI_ERR_SM_ENCLAVE_PMP_FAILURE);
 assert(enclaves[id].state==DESTROYING && active_regions==1);CASE("UTM descriptor free failure does not release EID");
 reset_fixture();id=create_fixture();region_id other;
 assert(!pmp_region_init_atomic((uintptr_t)other_page,sizeof other_page,PMP_PRI_ANY,&other,0));
 enclaves[id].regions[2]=(struct enclave_region){.pmp_rid=other,.type=REGION_OTHER};
 unset_fail_call=2;r=invoke(2002,id);normal_return(r,SBI_ERR_SM_ENCLAVE_PMP_FAILURE);
 assert(unset_calls==2 && free_calls==0 && active_regions==3 && enclaves[id].state==DESTROYING);
 CASE("later private-region unset failure retains all descriptors, including earlier successful unset");
 printf("SUMMARY SBI 2002: %d groups completed, %d known findings reproduced (hardware mocked)\n",cases,findings);return 0;
}
