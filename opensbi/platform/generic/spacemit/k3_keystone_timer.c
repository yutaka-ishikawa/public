/* SPDX-License-Identifier: BSD-2-Clause */
/* The current Keystone SM owns preemption through M_TIMER. Do not expose
	* direct Sstc timers until stimecmp state and its interrupt path are isolated.
	* This file is built only with CONFIG_K3_KEYSTONE_SM. */
#include <libfdt.h>
#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_bitops.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_heap.h>
#include <sbi/sbi_string.h>
#include <spacemit/k3_keystone_timer.h>

int k3_keystone_timer_extensions(const struct fdt_match *match,
                                 struct sbi_hart_features *features)
{
	(void)match;
	if (features->priv_version >= SBI_HART_PRIV_VER_1_12 &&
     __test_bit(SBI_HART_EXT_SSTC, features->extensions)) {
#if __riscv_xlen == 32
		csr_clear(CSR_MENVCFGH, ENVCFG_STCE >> 32);
#else
		csr_clear(CSR_MENVCFG, ENVCFG_STCE);
#endif
	}
	__clear_bit(SBI_HART_EXT_SSTC, features->extensions);
	return 0;
}

/* Accept the extension's optional numeric version suffix, but not a different
	* extension whose name merely starts with sstc. */
static bool is_sstc(const char *token, int len)
{
	return len >= 4 && !sbi_memcmp(token, "sstc", 4) &&
        (len == 4 || (token[4] >= '0' && token[4] <= '9'));
}

static int strip_sstc(void *fdt, int node, const char *name, bool stringlist)
{
	int len, input = 0, output = 0, count, rc;
	const char *property = fdt_getprop(fdt, node, name, &len);
	char *filtered;
	if (!property)
		return len == -FDT_ERR_NOTFOUND ? 0 : len;
	if (!len && stringlist)
		return 0;
	if (len <= 0 || property[len - 1] != '\0')
		return -FDT_ERR_BADVALUE;
	filtered = sbi_malloc(len);
	if (!filtered)
		return SBI_ENOMEM;
	while (input < len) {
		count = 0;
		while (input + count < len && property[input + count] &&
         (stringlist || property[input + count] != '_'))
			count++;
		if (!count || (!stringlist && input + count < len &&
                 property[input + count] == '_' && input + count + 1 == len - 1)) {
			sbi_free(filtered);
			return -FDT_ERR_BADVALUE;
		}
		if (!is_sstc(property + input, count)) {
			if (!stringlist && output)
    filtered[output++] = '_';
			sbi_memcpy(filtered + output, property + input, count);
			output += count;
			if (stringlist)
    filtered[output++] = '\0';
		}
		input += count + 1;
	}
	if (!stringlist)
		filtered[output++] = '\0';
	rc = fdt_setprop(fdt, node, name, filtered, output);
	sbi_free(filtered);
	return rc;
}

int k3_keystone_timer_fdt_fixup(void *fdt, const struct fdt_match *match)
{
	int cpus, node, rc;
	(void)match;
	cpus = fdt_path_offset(fdt, "/cpus");
	if (cpus < 0)
		return cpus;
	fdt_for_each_subnode(node, fdt, cpus) {
		int len;
		const char *type = fdt_getprop(fdt, node, "device_type", &len);
		if (!type || len != 4 || sbi_memcmp(type, "cpu", 4))
			continue;
		rc = strip_sstc(fdt, node, "riscv,isa", false);
		if (rc)
			return rc;
		rc = strip_sstc(fdt, node, "riscv,isa-extensions", true);
		if (rc)
			return rc;
	}
	return 0;
}
