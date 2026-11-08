#ifndef CRYPTO_VERIFY_PROTOCOL_H
#define CRYPTO_VERIFY_PROTOCOL_H
#include <stdint.h>
#include "shared/crypto_abi.h"
#define CRYPTO_CHALLENGE_CALL 1
#define CRYPTO_REPORT_CALL 2
#define CRYPTO_DONE_CALL 3
#define CRYPTO_REPLAY_CALL 4

struct crypto_challenge {
    uint8_t nonce[32];
    uint8_t key_digest[64];
};

struct crypto_message {
    uint64_t sequence;
    struct keystone_report report;
};
#endif
