#ifndef ROBUSTNESS_PROTOCOL_H
#define ROBUSTNESS_PROTOCOL_H
#include <stdint.h>
#include "shared/crypto_abi.h"
#include "../sbi-4000/protocol.h"
#define ROBUST_CALL 1
#define ROBUST_BAD_RETURN 2
#define ROBUST_ROUNDS 16
#define ROBUST_BYTES 257

struct robust_message {
    uint64_t worker;
    uint64_t generation;
    uint64_t sequence;
    int64_t rng_status;
    int64_t attest_status;
    int64_t seal_status;
    uint8_t bytes[ROBUST_BYTES];
    struct keystone_report report;
    uint8_t commitment[64];
    struct multimem_result multimem;
};

static inline unsigned char robust_byte(uint64_t worker, uint64_t generation,
					uint64_t sequence, size_t position)
{
    if (position < 8) {
	return worker >> (position * 8);
    }
    if (position < 16) {
	return generation >> ((position - 8) * 8);
    }
    if (position < 24) {
	return sequence >> ((position - 16) * 8);
    }
    return (unsigned char)(position * 17 + sequence + worker + generation);
}
#endif
