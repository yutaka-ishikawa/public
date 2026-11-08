#include "../common/harness.c"

int main(void)
{
    struct sbi_trap_regs r;
    unsigned long id;
    int epm_rid;

    reset_fixture();
    id = create_fixture();
    epm_rid = enclaves[id].regions[0].pmp_rid;
    r = invoke(2003, id);
    assert(r.a0 == 0 && enclave_context);
    utm[128] = 0x32;
    r = invoke(SBI_SM_STOP_ENCLAVE, STOP_EDGE_CALL_HOST);
    assert(r.a0 == SBI_ERR_SM_ENCLAVE_EDGE_CALL_HOST);
    assert(!enclave_context && enclaves[id].state == STOPPED);
    assert(local_permissions[epm_rid] == PMP_NO_PERM);
    assert(os_permissions == PMP_ALL_PERM);
    CASE("OCALL STOP returns EDGE_CALL_HOST and revokes EPM before host "
	 "context");

    assert(utm[128] == 0x32);
    utm[128] = 0x91;
    r = invoke(2005, id);
    assert(r.a0 == 0 && enclave_context);
    assert(utm[128] == 0x91 && local_permissions[epm_rid] == PMP_ALL_PERM);
    CASE("RESUME preserves host-written shared return data and restores "
	 "enclave mapping");

    printf("SUMMARY EDGE SM: %d groups passed, %d findings (PMP API mocked)\n",
	   cases, findings);
    return 0;
}
