#include "app/eapp_utils.h"
#include "app/syscall.h"
#include <string.h>
#include "verifier/ed25519/ed25519.h"
#include "common/sha3.h"
#include "protocol.h"
#ifndef VARIANT
#define VARIANT 0
#endif
static volatile unsigned int variant = VARIANT;
static unsigned char data[4097];
static struct crypto_challenge challenge;
static struct crypto_message message;

static struct {
    unsigned char before[8];
    unsigned char bytes[KEYSTONE_REPORT_BYTES];
    unsigned char after[8];
} output;

static struct {
    unsigned char before[8];
    struct sealing_key key;
    unsigned char after[8];
} sealed;
static struct sealing_key previous;
static const unsigned char identifier[] = "verify-sealing\0v1";

static int canaries(void)
{
    size_t i;

    for (i = 0; i < 8; i++) {
	if (output.before[i] != 0xa5 || output.after[i] != 0xa5 ||
	    sealed.before[i] != 0xa5 || sealed.after[i] != 0xa5) {
	    return 0;
	}
    }
    return 1;
}

int EAPP_ENTRY main(void)
{
    static const size_t lengths[] = {0, 1, 7, 8, 32, 1023, 1024};
    static const size_t ids[] = {0, 1, 7, 8, 4095, 4096};
    unsigned char digest[64];
    unsigned char signature[64];
    unsigned long answer;
    long rc;
    size_t i;
    size_t j;

    if (variant > 1) {
	EAPP_RETURN(1);
    }
    memset(&output, 0xa5, sizeof output);
    memset(&sealed, 0xa5, sizeof sealed);
    if (ocall(CRYPTO_CHALLENGE_CALL, 0, 0, &challenge, sizeof challenge)) {
	EAPP_RETURN(2);
    }
    if (attest_enclave_checked(output.bytes, 1351, data, 0) != -22 ||
	(long)attest_enclave(output.bytes, data, 1025) != -22 ||
	(long)attest_enclave(0, data, 1) != -14 ||
	(long)attest_enclave(output.bytes, 0, 1) != -14 ||
	(long)get_sealing_key(&sealed.key, 191, data, 1) != -22 ||
	(long)get_sealing_key(&sealed.key, 192, data, 4097) != -22) {
	EAPP_RETURN(3);
    }
#ifdef EXPECT_UNAVAILABLE
    if (attest_enclave(output.bytes, data, 0) != 100100 ||
	get_sealing_key(&sealed.key, sizeof sealed.key, data, 0) != 100100) {
	EAPP_RETURN(4);
    }
    for (i = 0; i < sizeof output.bytes; i++) {
	if (output.bytes[i] != 0xa5) {
	    EAPP_RETURN(5);
	}
    }
    if (ocall(CRYPTO_DONE_CALL, 0, 0, &answer, sizeof answer)) {
	EAPP_RETURN(6);
    }
    EAPP_RETURN(0);
#endif
    for (i = 3002; i <= 3003; i++) {
	for (j = 0; j < 9; j++) {
	    if ((long)SYSCALL_3(1099, i, j, 0) != 100008) {
		EAPP_RETURN(7);
	    }
	}
	if ((long)SYSCALL_3(1099, i, 9, 0)) {
	    EAPP_RETURN(8);
	}
    }
    for (i = 0; i < sizeof lengths / sizeof lengths[0]; i++) {
	for (j = 0; j < sizeof data; j++) {
	    data[j] = j < 32 ? challenge.nonce[j] : (unsigned char)(j ^ i);
	}
	memset(&output, 0xa5, sizeof output);
	rc = attest_enclave_checked(output.bytes, sizeof output.bytes, data,
				    lengths[i]);
	if (rc || !canaries()) {
	    EAPP_RETURN(10);
	}
	memcpy(&message.report, output.bytes, KEYSTONE_REPORT_BYTES);
	message.sequence = i;
	if (message.report.enclave.data_len != lengths[i] ||
	    memcmp(message.report.enclave.data, data, lengths[i])) {
	    EAPP_RETURN(11);
	}
	if (ocall(CRYPTO_REPORT_CALL, &message, sizeof message, &answer,
		  sizeof answer) ||
	    answer) {
	    EAPP_RETURN(12);
	}
    }
    if (ocall(CRYPTO_REPLAY_CALL, &message, sizeof message, &answer,
	      sizeof answer) ||
	answer != 1) {
	EAPP_RETURN(13);
    }
    rc = get_sealing_key(&sealed.key, sizeof sealed.key, (void *)identifier,
			 sizeof identifier - 1);
    if (rc || !canaries()) {
	EAPP_RETURN(20);
    }
    previous = sealed.key;
    sha3(sealed.key.key, sizeof sealed.key.key, digest, 64);
    if (memcmp(digest, challenge.key_digest, 64) ||
	!ed25519_verify(sealed.key.signature, sealed.key.key, 128,
			message.report.sm.public_key)) {
	EAPP_RETURN(21);
    }
    memcpy(signature, sealed.key.signature, 64);
    signature[0] ^= 1;
    if (ed25519_verify(signature, sealed.key.key, 128,
		       message.report.sm.public_key)) {
	EAPP_RETURN(22);
    }
    if (get_sealing_key(&sealed.key, 193, (void *)identifier,
			sizeof identifier - 1) ||
	memcmp(&previous, &sealed.key, sizeof previous) || !canaries()) {
	EAPP_RETURN(23);
    }
    for (i = 0; i < sizeof ids / sizeof ids[0]; i++) {
	if (get_sealing_key(&sealed.key, sizeof sealed.key, data, ids[i]) ||
	    !ed25519_verify(sealed.key.signature, sealed.key.key, 128,
			    message.report.sm.public_key) ||
	    !canaries()) {
	    EAPP_RETURN(24);
	}
    }
    if (!memcmp(previous.key, sealed.key.key, 128)) {
	EAPP_RETURN(25);
    }
    if (ocall(CRYPTO_DONE_CALL, 0, 0, &answer, sizeof answer) || answer) {
	EAPP_RETURN(26);
    }
    EAPP_RETURN(0);
}
