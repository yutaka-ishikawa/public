// Derived from keystone/examples/attestation; see ../LICENSE.
#include "app/eapp_utils.h"
#include "app/syscall.h"
#include "edge/edge_common.h"
#include "shared/crypto_abi.h"

int EAPP_ENTRY main(void)
{
    struct edge_data data;
    struct keystone_report report;
    unsigned char nonce[1024];
    unsigned long i;

    if (ocall(4, 0, 0, &data, sizeof data) || data.size > sizeof nonce ||
	!data.size) {
	EAPP_RETURN(1);
    }
    for (i = 1; i <= 10000; i++) {
	if (i % 5000 == 0) {
	    if (ocall(2, &i, sizeof i, 0, 0)) {
		EAPP_RETURN(3);
	    }
	}
    }
    if (copy_from_shared(nonce, data.offset, data.size)) {
	EAPP_RETURN(1);
    }
    if (attest_enclave_checked(&report, sizeof report, nonce, data.size) ||
	ocall(3, &report, sizeof report, 0, 0)) {
	EAPP_RETURN(2);
    }
    EAPP_RETURN(0);
}
