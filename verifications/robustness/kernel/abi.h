#ifndef ROBUSTNESS_KERNEL_ABI_H
#define ROBUSTNESS_KERNEL_ABI_H
#include <linux/ioctl.h>
#include <linux/types.h>

struct robust_init {
    __u64 pointers[3];
    __u64 lengths[3];
    __u64 eid;
};

struct robust_call {
    __u64 fid;
    __u64 operation;
    __u64 stage;
    __u64 ticks;
    __s64 error;
    __u64 value;
};

#define ROBUST_INIT _IOWR('K', 0xf0, struct robust_init)
#define ROBUST_CALL_RAW _IOWR('K', 0xf1, struct robust_call)
#endif
