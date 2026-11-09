#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "client.h"
#include "verifier/ed25519/ed25519.h"

int check_report(const struct keystone_report *report,
		 const unsigned char *nonce, const unsigned char *measurement,
		 const struct configuration *config)
{
    if (report->enclave.data_len != 32 ||
	memcmp(report->enclave.data, nonce, 32) ||
	memcmp(report->enclave.hash, measurement, 64) ||
	memcmp(report->dev_public_key, config->device_key, 32) ||
	memcmp(report->sm.hash, config->sm_hash, 64) ||
	!ed25519_verify(report->sm.signature,
			(const unsigned char *)&report->sm, 96,
			config->device_key) ||
	!ed25519_verify(report->enclave.signature,
			(const unsigned char *)&report->enclave, 64 + 8 + 32,
			report->sm.public_key)) {
	fprintf(stderr,
		"FAIL SBI 3002 report pins/signatures/nonce/measurement\n");
	return -1;
    }
    return 0;
}
