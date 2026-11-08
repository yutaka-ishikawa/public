#ifndef RNG_VERIFY_PROTOCOL_H
#define RNG_VERIFY_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#define RNG_CALL 1
#define RNG_MAX 4096

struct rng_message {
    uint64_t sequence;
    uint64_t length;
    int64_t status;
    unsigned char bytes[RNG_MAX];
};
#endif
