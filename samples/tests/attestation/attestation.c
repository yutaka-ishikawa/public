// Derived from keystone/examples/tests/attestation; see ../LICENSE.
#include "app/eapp_utils.h"
#include "app/syscall.h"
#include "shared/crypto_abi.h"

int EAPP_ENTRY main(void)
{
    struct keystone_report report;
    char nonce[] = "nonce";

    if (attest_enclave_checked(&report, sizeof report, nonce, 5) ||
	ocall(3, &report, sizeof report, 0, 0)) {
	EAPP_RETURN(1);
    }
    EAPP_RETURN(0);
}
