#ifndef EDGE_VERIFY_PROTOCOL_H
#define EDGE_VERIFY_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#define EDGE_VERIFY_CALL 1
#define EDGE_VERIFY_BAD_RETURN 2
#define EDGE_VERIFY_MAX 4096

struct edge_verify_message {
    uint64_t sequence;
    uint64_t private_va;
    uint64_t length;
    unsigned char bytes[EDGE_VERIFY_MAX];
};

#define EDGE_VERIFY_HEADER offsetof(struct edge_verify_message, bytes)
#endif
