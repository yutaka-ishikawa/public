#ifndef KEYSTONE_VERIFY_SBI_ABI_H
#define KEYSTONE_VERIFY_SBI_ABI_H
#include <linux/ioctl.h>
#include <linux/types.h>
#define KEYSTONE_VERIFY_SBI_COUNT 9

struct keystone_verify_sbi {
    __s64 error[KEYSTONE_VERIFY_SBI_COUNT];
    __s64 value[KEYSTONE_VERIFY_SBI_COUNT];
};

#define KEYSTONE_VERIFY_SBI _IOR('K', 0xe2, struct keystone_verify_sbi)
#endif
