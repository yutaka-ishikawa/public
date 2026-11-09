#ifndef MULTIMEM_KERNEL_ABI_H
#define MULTIMEM_KERNEL_ABI_H
#include <linux/types.h>
#include <linux/ioctl.h>
#define MULTIMEM_KERNEL_CASES 10

struct multimem_kernel_result {
    __s64 error[MULTIMEM_KERNEL_CASES];
};

#define MULTIMEM_KERNEL_TEST _IOR('K', 0xe4, struct multimem_kernel_result)

struct multimem_overlap {
    __u64 paddr;
    __u64 size;
    __s64 error;
};

#define MULTIMEM_KERNEL_OVERLAP _IOWR('K', 0xe5, struct multimem_overlap)
#endif
