/* SPDX-License-Identifier: BSD-2-Clause */
/* Register layout and clock sequence reference: SpacemiT OP-TEE K3,
 * k3_prng.c/k3_ccu.c, commit 2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59.
 * k3_ccu.c: Copyright (C) 2025 SpacemiT Tech. Co., Ltd. */
#ifndef KEYSTONE_K3_RNG_REGS_H
#define KEYSTONE_K3_RNG_REGS_H
#define K3_RNG_MAP_BASE 0xf0703000UL
#define K3_RNG_MAP_SIZE 0x1000
#define K3_RNG_OFFSET 0x800
#define K3_RNG_FIFO 0x44
#define K3_RNG_CTRL 0xc0
#define K3_RNG_DATA 0xc4
#define K3_RNG_TRNG_CTRL 0xd0
#define K3_RNG_ENABLE 1U
#define K3_RNG_VALID (1U << 31)
#define K3_RNG_FIFO_CLEAR (1U << 30)
#define K3_RNG_MAIN_BASE 0xd4050000UL
#define K3_RNG_MAIN_SIZE 0x3000
#define K3_RNG_MAIN_CLOCK 0x1024
#define K3_RNG_AP_BASE 0xd4282000UL
#define K3_RNG_AP_SIZE 0x1000
#define K3_RNG_AP_CLOCK 0x868
#define K3_RNG_AP_CLOCK_VALUE 0x1cff003fU
#endif
