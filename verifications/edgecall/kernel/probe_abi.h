#ifndef EDGE_VERIFY_PROBE_ABI_H
#define EDGE_VERIFY_PROBE_ABI_H
#include <linux/ioctl.h>
#include <linux/types.h>

struct edge_verify_probe {
    __u64 private_pa;
    __u64 shared_pa;
    __s32 private_result;
    __s32 shared_result;
    __s32 control_result;
    __u8 shared_byte;
    __u8 control_byte;
    __u8 private_byte;
    __u8 reserved;
};

#define EDGE_VERIFY_PROBE _IOWR('K', 0xe1, struct edge_verify_probe)
#endif
