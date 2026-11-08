#ifndef STOP_VERIFY_PROTOCOL_H
#define STOP_VERIFY_PROTOCOL_H
#include <stdint.h>
#define STOP_VERIFY_SYSCALL 1100
#define STOP_VERIFY_CALL 1
#define STOP_VERIFY_REPLY UINT64_C(0x5a172bc48def9036)
#define STOP_VERIFY_STAGES 9
struct stop_verify_message {
    uint64_t sequence;
    uint64_t request;
    uint64_t status;
};
#endif
