#include "app/eapp_utils.h"
#include "app/syscall.h"
#include "protocol.h"

static unsigned char storage[RNG_MAX + 2];
static struct rng_message message;

int EAPP_ENTRY main(void)
{
    static const size_t lengths[] = {0, 1, 7, 8, 9, 15, 16, 17, 32, 4096};
    struct rng_message response;
    long result;
    size_t i;
    size_t j;

    if ((long)SYSCALL_3(278, storage, 1, 4) != -22 ||
	(long)SYSCALL_3(278, 0, 1, 0) != -14 ||
	(long)SYSCALL_3(278, 0, 0, 0) != 0) {
	EAPP_RETURN(1);
    }
    for (i = 0; i < sizeof lengths / sizeof lengths[0]; i++) {
	for (j = 0; j < sizeof storage; j++) {
	    storage[j] = 0xa5;
	}
	result = (long)SYSCALL_3(278, storage + 1, lengths[i], i % 4);
#ifdef EXPECT_ERROR
	if (i == 0) {
	    if (result != 0) {
		EAPP_RETURN(2);
	    }
	    continue;
	}
	if (result >= 0) {
	    EAPP_RETURN(3);
	}
#else
	if (result != (long)lengths[i]) {
	    EAPP_RETURN(4);
	}
#endif
	if (storage[0] != 0xa5 || storage[lengths[i] + 1] != 0xa5) {
	    EAPP_RETURN(5);
	}
	message.sequence = i;
	message.length = lengths[i];
	message.status = result;
	for (j = 0; j < lengths[i]; j++) {
	    message.bytes[j] = storage[j + 1];
	}
	if (ocall(RNG_CALL, &message, sizeof message, &response,
		  sizeof response)) {
	    EAPP_RETURN(6);
	}
	if (response.sequence != i || response.length != lengths[i]) {
	    EAPP_RETURN(7);
	}
	for (j = 0; j < lengths[i]; j++) {
	    if (message.bytes[j] != storage[j + 1] ||
		response.bytes[j] != (unsigned char)(storage[j + 1] ^ 0x5a)) {
		EAPP_RETURN(8);
	    }
	}
#ifdef EXPECT_ERROR
	break;
#endif
    }
    EAPP_RETURN(0);
}
