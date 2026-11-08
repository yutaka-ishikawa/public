/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef KEYSTONE_RNG_H
#define KEYSTONE_RNG_H
#include <sbi/sbi_types.h>
#define KEYSTONE_RNG_AUTO 0
#define KEYSTONE_RNG_ZKR 1
#define KEYSTONE_RNG_JUPITER2 2
#define KEYSTONE_RNG_COUNTER 3

struct keystone_rng_ops {
    const char *name;
    const char *quality;
    int (*read64)(uint64_t *value);
};

int keystone_rng_init(void);
int keystone_rng_read64(uint64_t *value);
int platform_rng_read_counter(uint64_t *value);
const struct keystone_rng_ops *platform_rng_get_ops(void);
#endif
