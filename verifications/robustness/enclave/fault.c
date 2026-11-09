#include "app/eapp_utils.h"
#include "app/syscall.h"
#include "protocol.h"
#include <string.h>
#ifndef FAULT_STAGE
#error FAULT_STAGE is required
#endif
static struct {
    unsigned char before[8];
    unsigned char bytes[KEYSTONE_REPORT_BYTES];
    unsigned char after[8];
} output;
#if FAULT_STAGE != 10
static unsigned char identifier[32] = "robustness-fault-recovery";
#endif

int EAPP_ENTRY main(void)
{
    long first;
    long second;
    size_t length;
    size_t i;

    memset(&output, 0xa5, sizeof output);
#if FAULT_STAGE == 10
    length = 32;
    first = (long)SYSCALL_3(278, output.bytes, length, 0);
    if (first != -5) {
	EAPP_RETURN(1);
    }
#elif FAULT_STAGE == 13 || defined(FAULT_SEAL)
    length = KEYSTONE_SEAL_BYTES;
    first = get_sealing_key((struct sealing_key *)output.bytes, length,
			    identifier, sizeof identifier);
    if (first != (FAULT_STAGE == 13 ? 100000 : 100008)) {
	EAPP_RETURN(2);
    }
#else
    length = KEYSTONE_REPORT_BYTES;
    first = attest_enclave_checked(output.bytes, length, identifier,
				   sizeof identifier);
    if (first != 100008) {
	EAPP_RETURN(3);
    }
#endif
    /* At these injected pre-copy boundaries, failed calls must not copy out. */
    for (i = 0; i < sizeof output.bytes; i++) {
	if (output.bytes[i] != 0xa5) {
	    EAPP_RETURN(4);
	}
    }
#if FAULT_STAGE == 10
    second = (long)SYSCALL_3(278, output.bytes, length, 0);
    if (second != 32) {
	EAPP_RETURN(5);
    }
#elif FAULT_STAGE == 13 || defined(FAULT_SEAL)
    second = get_sealing_key((struct sealing_key *)output.bytes, length,
			     identifier, sizeof identifier);
    if (second) {
	EAPP_RETURN(6);
    }
#else
    second = attest_enclave_checked(output.bytes, length, identifier,
				    sizeof identifier);
    if (second) {
	EAPP_RETURN(7);
    }
#endif
    for (i = 0; i < 8; i++) {
	if (output.before[i] != 0xa5 || output.after[i] != 0xa5) {
	    EAPP_RETURN(8);
	}
    }
    for (i = length; i < sizeof output.bytes; i++) {
	if (output.bytes[i] != 0xa5) {
	    EAPP_RETURN(9);
	}
    }
    EAPP_RETURN(0);
}
