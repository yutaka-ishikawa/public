#include <sys/auxv.h>
#include "app/syscall.h"
#include "protocol.h"

int main(void)
{
    struct rng_message message = {0};
    struct rng_message response = {0};
    const unsigned char *random;
    size_t i;

    random = (const unsigned char *)getauxval(AT_RANDOM);
    if (!random) {
	return 1;
    }
    message.sequence = 0;
    message.length = 16;
    message.status = 16;
    for (i = 0; i < 16; i++) {
	message.bytes[i] = random[i];
    }
    if (ocall(RNG_CALL, &message, sizeof message, &response, sizeof response)) {
	return 2;
    }
    if (response.sequence || response.length != 16) {
	return 3;
    }
    for (i = 0; i < 16; i++) {
	if (random[i] != message.bytes[i] ||
	    response.bytes[i] != (unsigned char)(random[i] ^ 0x5a)) {
	    return 4;
	}
    }
    return 0;
}
