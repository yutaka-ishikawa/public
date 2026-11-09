#include "app/eapp_utils.h"
#include "app/syscall.h"
#include "common/sha3.h"
#include "protocol.h"
#include <string.h>

static struct robust_message message;
static struct robust_message response;
static struct sealing_key key;
static struct sealing_key previous;
static unsigned char random_bytes[34];
static const uint64_t readonly_word __attribute__((aligned(4096))) =
    0x9a25bc78d136ef40ULL;

int EAPP_ENTRY main(void)
{
    size_t i;
    size_t j;
    long rc;

    /* The first OCALL binds this instance to a coordinator generation. */
    memset(&message, 0, sizeof message);
    message.sequence = UINT64_MAX;
    if (ocall(ROBUST_CALL, &message, sizeof message, &response,
	      sizeof response)) {
	EAPP_RETURN(1);
    }
    message.worker = response.worker;
    message.generation = response.generation;
    for (i = 0; i < ROBUST_ROUNDS; i++) {
	message.sequence = i;
	for (j = 0; j < sizeof message.bytes; j++) {
	    message.bytes[j] =
		robust_byte(message.worker, message.generation, i, j);
	}
	/* These must fail before a provider can write outside its buffer. */
	if ((long)SYSCALL_3(278, random_bytes + 1, 1, 4) != -22 ||
	    (long)SYSCALL_3(278, 0, 1, 0) != -14 ||
	    attest_enclave_checked(&message.report, sizeof message.report - 1,
				   message.bytes, 32) != -22 ||
	    get_sealing_key(&key, sizeof key - 1, message.bytes, 32) != -22) {
	    EAPP_RETURN(2);
	}
	memset(random_bytes, 0xa5, sizeof random_bytes);
	rc = (long)SYSCALL_3(278, random_bytes + 1, 32, 0);
	message.rng_status = rc;
	if (random_bytes[0] != 0xa5 || random_bytes[33] != 0xa5) {
	    EAPP_RETURN(3);
	}
	memset(&message.report, 0xa5, sizeof message.report);
	message.attest_status = attest_enclave_checked(
	    &message.report, sizeof message.report, message.bytes, 32);
	memset(&key, 0xa5, sizeof key);
	message.seal_status =
	    get_sealing_key(&key, sizeof key, message.bytes, 32);
	if (!message.seal_status) {
	    if (get_sealing_key(&previous, sizeof previous, message.bytes,
				32) ||
		memcmp(key.key, previous.key, sizeof key.key)) {
		EAPP_RETURN(4);
	    }
	    sha3(key.key, sizeof key.key, message.commitment, 64);
	}
	if (SYSCALL_2(1098, &message.multimem, &readonly_word) ||
	    *(volatile const uint64_t *)&readonly_word !=
		0x9a25bc78d136ef40ULL) {
	    EAPP_RETURN(7);
	}
	/* Deliberately hold U-mode for MTIMER preemption, without rdcycle. */
	{
	    unsigned long start;
	    unsigned long now;

	    asm volatile("rdtime %0" : "=r"(start));
	    do {
		asm volatile("rdtime %0" : "=r"(now));
	    } while (now - start < 20000);
	}
	if (ocall(ROBUST_CALL, &message, sizeof message, &response,
		  sizeof response) ||
	    response.worker != message.worker ||
	    response.generation != message.generation ||
	    response.sequence != i) {
	    EAPP_RETURN(5);
	}
	for (j = 0; j < sizeof message.bytes; j++) {
	    if (response.bytes[j] != (unsigned char)(message.bytes[j] ^ 0x5a)) {
		EAPP_RETURN(6);
	    }
	}
    }
    memset(&response, 0xcc, sizeof response);
    if (!ocall(ROBUST_BAD_RETURN, &message, sizeof message, &response,
	       sizeof response)) {
	EAPP_RETURN(8);
    }
    for (j = 0; j < sizeof response; j++) {
	if (((unsigned char *)&response)[j] != 0xcc) {
	    EAPP_RETURN(9);
	}
    }
    memset(&key, 0, sizeof key);
    memset(&previous, 0, sizeof previous);
    EAPP_RETURN(0);
}
