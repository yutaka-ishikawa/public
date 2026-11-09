#ifndef MULTIMEM_VERIFY_PROTOCOL_H
#define MULTIMEM_VERIFY_PROTOCOL_H
#include <stdint.h>
#define MULTIMEM_PROBES 8

struct multimem_result {
    uintptr_t size_status;
    uintptr_t addr_status;
    uintptr_t size;
    uintptr_t addr;
    uintptr_t status[MULTIMEM_PROBES];
};
#endif
