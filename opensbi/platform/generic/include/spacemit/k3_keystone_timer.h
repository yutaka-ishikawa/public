/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef K3_KEYSTONE_TIMER_H
#define K3_KEYSTONE_TIMER_H
#include <sbi/sbi_hart.h>
struct fdt_match;
int k3_keystone_timer_extensions(const struct fdt_match *match,
                                 struct sbi_hart_features *features);
int k3_keystone_timer_fdt_fixup(void *fdt, const struct fdt_match *match);
#endif
