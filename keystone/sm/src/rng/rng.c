#include "verification.h"
#include "cpu.h"
/* SPDX-License-Identifier: BSD-2-Clause */
#include "rng.h"
#include "sm_err.h"
#include "k3_rng_regs.h"
#include "sha3/sha3.h"
#include <sbi/riscv_asm.h>
#include <sbi/riscv_io.h>
#include <sbi/riscv_locks.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_timer.h>
#include <sbi/sbi_string.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <libfdt.h>
#ifndef KEYSTONE_RNG_BACKEND
#define KEYSTONE_RNG_BACKEND KEYSTONE_RNG_AUTO
#endif
#define RNG_POLLS 10000U
static const struct keystone_rng_ops *selected;
static int init_status = SBI_ERR_SM_NOT_IMPLEMENTED;
static spinlock_t rng_lock = SPIN_LOCK_INITIALIZER;
static uint64_t sequence;

static int zkr_available(void)
{
    return sbi_hart_has_extension(sbi_scratch_thishart_ptr(), SBI_HART_EXT_ZKR);
}

static int wtm_available(void)
{
    const void *fdt;
    const fdt32_t *reg;
    int node;
    int len;

    fdt = fdt_get_address();
    node = fdt_node_offset_by_compatible(fdt, -1, "spacemit,k3-wtm-rng");
    if (node < 0 || !fdt_node_is_enabled(fdt, node)) {
	return 0;
    }
    reg = fdt_getprop(fdt, node, "reg", &len);
    /* Only the known K3 window is admitted. No arbitrary MMIO discovery. */
    return reg && len == 16 && fdt32_to_cpu(reg[0]) == 0 &&
	   fdt32_to_cpu(reg[1]) == K3_RNG_MAP_BASE &&
	   fdt32_to_cpu(reg[2]) == 0 && fdt32_to_cpu(reg[3]) == K3_RNG_MAP_SIZE;
}

int __attribute__((weak)) platform_rng_read_counter(uint64_t *value)
{
    const struct sbi_timer_device *timer;

    timer = sbi_timer_get_device();
    if (!sbi_hart_has_extension(sbi_scratch_thishart_ptr(),
				SBI_HART_EXT_ZICNTR) &&
	(!timer || !timer->timer_value)) {
	return SBI_ERR_SM_NOT_IMPLEMENTED;
    }
    *value = sbi_timer_value();
    return 0;
}

static int counter_read(uint64_t *value)
{
    uint64_t input[4];
    uint8_t digest[32];
    int rc;

    rc = platform_rng_read_counter(&input[0]);
    if (rc) {
	return rc;
    }
    input[1] = csr_read(CSR_MHARTID);
    input[2] = ++sequence;
    input[3] = 0x434f554e54455231UL;
    sha3(input, sizeof input, digest, sizeof digest);
    sbi_memcpy(value, digest, sizeof *value);
    sbi_memset(digest, 0, sizeof digest);
    return 0;
}

static int zkr_read(uint64_t *value)
{
    unsigned long seed;
    unsigned int attempt;
    unsigned int words;
    uint64_t result;

    if (!zkr_available()) {
	return SBI_ERR_SM_NOT_IMPLEMENTED;
    }
    result = 0;
    words = 0;
    for (attempt = 0; attempt < RNG_POLLS; attempt++) {
	/* Zkr requires a read/write CSR instruction, not CSRRS x0. */
	seed = csr_swap(0x015, 0);
	switch ((seed >> 30) & 3) {
	case 2:
	    result |= (uint64_t)(seed & 0xffff) << (words * 16);
	    if (++words == 4) {
		*value = result;
		return 0;
	    }
	    break;
	case 3:
	    return SBI_ERR_SM_RNG_FAILURE;
	default:
	    break;
	}
    }
    return SBI_ERR_SM_RNG_NOT_READY;
}

static int wtm_clock(void)
{
    uintptr_t main_clock;
    uintptr_t ap_clock;
    uint32_t value;

    main_clock = K3_RNG_MAIN_BASE + K3_RNG_MAIN_CLOCK;
    ap_clock = K3_RNG_AP_BASE + K3_RNG_AP_CLOCK;
    value = readl((void *)main_clock);
    writel(value | (1U << 5), (void *)main_clock);
    writel(K3_RNG_AP_CLOCK_VALUE, (void *)ap_clock);
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    if (!(readl((void *)main_clock) & (1U << 5)) ||
	readl((void *)ap_clock) != K3_RNG_AP_CLOCK_VALUE) {
	return SBI_ERR_SM_RNG_FAILURE;
    }
    return 0;
}

static int wtm_word(uint32_t *word)
{
    uintptr_t base;
    uint64_t start;
    uint64_t now;
    uint64_t frequency;
    unsigned int attempt;
    uint32_t value;

    base = K3_RNG_MAP_BASE + K3_RNG_OFFSET;
    start = sbi_timer_value();
    frequency = sbi_timer_get_device() ? sbi_timer_get_device()->timer_freq : 0;
    for (attempt = 0; attempt < RNG_POLLS; attempt++) {
	value = readl((void *)(base + K3_RNG_CTRL));
	if (value & K3_RNG_VALID) {
	    value = readl((void *)(base + K3_RNG_FIFO));
	    writel(value | K3_RNG_FIFO_CLEAR, (void *)(base + K3_RNG_FIFO));
	    __asm__ volatile("fence iorw, iorw" ::: "memory");
	    *word = readl((void *)(base + K3_RNG_DATA));
	    return 0;
	}
	now = sbi_timer_value();
	if (frequency && now - start >= frequency / 10) {
	    break;
	}
    }
    return SBI_ERR_SM_RNG_NOT_READY;
}

static int wtm_read(uint64_t *value)
{
    uintptr_t base;
    uint32_t words[2];
    uint32_t ctrl;
    int rc;

    rc = wtm_clock();
    if (rc) {
	return rc;
    }
    base = K3_RNG_MAP_BASE + K3_RNG_OFFSET;
    ctrl = readl((void *)(base + K3_RNG_CTRL));
    if (!(ctrl & K3_RNG_ENABLE)) {
	ctrl = readl((void *)(base + K3_RNG_TRNG_CTRL));
	writel(ctrl & ~((1U << 4) | (1U << 6)),
	       (void *)(base + K3_RNG_TRNG_CTRL));
	writel(K3_RNG_ENABLE, (void *)(base + K3_RNG_CTRL));
    }
    rc = wtm_word(&words[0]);
    if (!rc) {
	rc = wtm_word(&words[1]);
    }
    if (!rc) {
	*value = words[0] | ((uint64_t)words[1] << 32);
    }
    return rc;
}

static const struct keystone_rng_ops backends[] = {
    {"zkr", "hardware-source", zkr_read},
    {"jupiter2", "hardware-prng", wtm_read},
    {"counter", "development", counter_read},
};

const struct keystone_rng_ops *__attribute__((weak)) platform_rng_get_ops(void)
{
    return NULL;
}

int keystone_rng_init(void)
{
    uint64_t probe;
    unsigned int choice;
    int request;

    request = KEYSTONE_RNG_BACKEND;
    selected = platform_rng_get_ops();
    if (!selected) {
	if (request == KEYSTONE_RNG_AUTO) {
	    request = zkr_available()	? KEYSTONE_RNG_ZKR
		      : wtm_available() ? KEYSTONE_RNG_JUPITER2
					: KEYSTONE_RNG_COUNTER;
	}
	choice = request - 1;
	if (choice >= sizeof backends / sizeof backends[0]) {
	    return init_status;
	}
	selected = &backends[choice];
    }
    if ((request == KEYSTONE_RNG_ZKR && !zkr_available()) ||
	(request == KEYSTONE_RNG_JUPITER2 && !wtm_available())) {
	init_status = SBI_ERR_SM_NOT_IMPLEMENTED;
    } else {
	init_status = selected->read64(&probe);
    }
    sbi_printf("[SM/RNG] selected=%s quality=%s status=%d\n", selected->name,
	       selected->quality, init_status);
    return init_status;
}

int keystone_rng_read64(uint64_t *value)
{
    int rc;

    *value = 0;
    spin_lock(&rng_lock);
    rc = init_status;
    if (!rc && selected) {
        if (sm_verify_fault(10, cpu_get_enclave_id())) {
            rc = SBI_ERR_SM_RNG_FAILURE;
        } else {
            rc = selected->read64(value);
        }
    }
    if (rc) {
	*value = 0;
    }
    spin_unlock(&rng_lock);
    return rc;
}
