/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *   Anup Patel <anup.patel@wdc.com>
 */

#include <sbi/riscv_asm.h>
#include <sbi/riscv_atomic.h>
#include <sbi/riscv_barrier.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_cppc.h>
#include <sbi/sbi_domain.h>
#include <sbi/sbi_ecall.h>
#include <sbi/sbi_fwft.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_hartmask.h>
#include <sbi/sbi_heap.h>
#include <sbi/sbi_hsm.h>
#include <sbi/sbi_ipi.h>
#include <sbi/sbi_irqchip.h>
#include <sbi/sbi_platform.h>
#include <sbi/sbi_pmu.h>
#include <sbi/sbi_dbtr.h>
#include <sbi/sbi_mpxy.h>
#include <sbi/sbi_sse.h>
#include <sbi/sbi_system.h>
#include <sbi/sbi_string.h>
#include <sbi/sbi_timer.h>
#include <sbi/sbi_tlb.h>
#include <sbi/sbi_version.h>
#include <sbi/sbi_unit_test.h>

/* K3 A1-E/E2: local PMP protection must be verified before each Hart is
 * admitted to S-mode. STOPPED Harts require local validation on their later
 * HSM Start; they are not treated as completed IPI targets. The existing
 * cold/warm/resume gate remains fail-closed until the actual PMP allocation
 * and required global policy consistency are established. No all-Hart M-mode
 * HOLD/RELEASE scheme is implied by this gate.
 */
#ifdef CONFIG_K3_KEYSTONE_SM
extern int k3_pmp_sync_initialize(void);
extern int sm_init(bool cold_boot);
extern int k3_keystone_allow_initial_hsm_starts(void);
extern int k3_pmp_boot_partition_admit(unsigned short, unsigned short, int);
#include <sbi/k3_a1_csr_preflight.h>

static void k3_keystone_a1_post_pmp_before_smode(struct sbi_scratch *scratch,
                                                  u32 hartid)
{
	struct k3_a1_local_pmp_snapshot snapshot;
	int rc;

	rc = sbi_hart_keystone_a1_read_pmp_snapshot(scratch, &snapshot);
	if (rc) {
		sbi_printf("K3 A1-E: PMP snapshot failed on Hart %u: %d\n", hartid, rc);
		sbi_hart_hang();
	}

	/* Optional serial inventory for a dedicated, authorized M-mode
	 * diagnostic run. Read-only: never writes CSR, never exposes key data.
	 * Deliberately disabled unless the diagnostic macro is explicitly set.
	 * A log is evidence for review, not an authenticated security proof. */
#ifdef K3_A1_DIAG_PMP_SNAPSHOT
	{
		unsigned int i;
		sbi_printf("K3_A1_CSR hart=%u count=%u gran=%u mseccfg_valid=%u mseccfg=%lx\n",
		           hartid, snapshot.pmp_count, snapshot.pmp_log2gran,
		           snapshot.mseccfg_valid, snapshot.mseccfg);
		for (i = 0; i < snapshot.pmp_count; ++i)
			sbi_printf("K3_A1_CSR hart=%u idx=%u prot=%lx addr=%lx log2len=%lu\n",
			           hartid, i, snapshot.entry[i].prot,
			           snapshot.entry[i].addr, snapshot.entry[i].log2len);
	}
#endif
	/* Inspect *actual local* legacy-PMP shape before recording a local
	 * observation. This is preflight, not an authorization to hand over. */
	{
		struct k3_a1_csr_measured measured = {0};
		struct k3_a1_local_candidate candidate = {0};
		unsigned int i;
		measured.count = snapshot.pmp_count;
		measured.granularity = snapshot.pmp_log2gran;
		measured.pmp_addrbits = sbi_hart_pmp_addrbits(scratch);
		measured.mseccfg = snapshot.mseccfg;
		measured.mseccfg_valid = snapshot.mseccfg_valid;
		for (i = 0; i < snapshot.pmp_count; i++) {
			measured.entry[i].prot = snapshot.entry[i].prot;
			measured.entry[i].addr = snapshot.entry[i].addr;
			measured.entry[i].log2len = snapshot.entry[i].log2len;
		}
		rc = k3_a1_preflight_local(&measured, &candidate);
		if (rc) {
			sbi_printf("K3 A1-E: local PMP shape rejected on Hart %u: %d\n",
			           hartid, rc);
			sbi_hart_hang();
		}
		/* A1-E boot partition: admit only the already-migrated local CSR
		 * layout. The cold-boot Hart fixes the common masks; a subsequent
		 * Hart must match exactly. This is NOT enclave readiness. */
		if (candidate.fallback != 15U || candidate.old_allow != 15U ||
		    k3_pmp_boot_partition_admit(
		      (unsigned short)(candidate.firmware_entries | 0x8000U),
		      candidate.free_keystone_entries,
		      hartid == sbi_domain_thishart_ptr()->boot_hartid)) {
			sbi_printf("K3 A1-E: boot partition not committed on Hart %u\n", hartid);
			sbi_hart_hang();
		}
	}
	/* E2: record local observation only. No STOPPED Hart is an ACK. */
	{
		extern int sbi_hsm_k3_e2_note_local_snapshot(unsigned int);
		if (sbi_hsm_k3_e2_note_local_snapshot(
				sbi_hartid_to_hartindex(hartid))) sbi_hart_hang();
	}

	/* The common slot mask is now committed. This is not SM readiness:
	 * the caller MUST run sm_init() and reject S-mode entry unless SM
	 * PMP memory isolation is independently established. */
	(void)snapshot;
	return;
}
#endif

/* K3 migration evidence acquisition: normal OpenSBI only, read-only.
 * Build explicitly with K3_PMP_DIAG_READONLY=1. The diagnostic is NOT a
 * proof of DMA/IOPMP protection, and STOPPED Harts produce no snapshot
 * until they are started by the existing HSM mechanism. */
#if defined(K3_PMP_DIAG_READONLY) && defined(CONFIG_PLATFORM_SPACEMIT_K3) && !defined(CONFIG_K3_KEYSTONE_SM)
static void k3_pmp_diag_readonly(struct sbi_scratch *scratch, u32 hartid)
{
    unsigned int i, count = sbi_hart_pmp_count(scratch);
    unsigned long prot, base, log2len;
    int rc;
    /* Vendor Smepmp mode can revoke M-mode access to console MMIO once
     * configured. Do not risk a post-PMP console fault just for evidence.
     * Its installed PMP state must be gathered with a separate verified
     * diagnostic path; a missing record is never treated as success. */
    if (sbi_hart_has_extension(scratch, SBI_HART_EXT_SMEPMP))
        return;
    sbi_printf("K3_PMP_DIAG hart=%u count=%u gran=%u smepmp=%u\n",
               hartid, count, sbi_hart_pmp_log2gran(scratch),
               sbi_hart_has_extension(scratch, SBI_HART_EXT_SMEPMP) ? 1U : 0U);
    /* Report all implemented entries; no guessed fixed 8/16 slot count. */
    for (i = 0; i < count; i++) {
        rc = pmp_get(i, &prot, &base, &log2len);
        if (rc) {
            sbi_printf("K3_PMP_DIAG hart=%u idx=%u error=%d\n", hartid, i, rc);
            continue;
        }
        sbi_printf("K3_PMP_DIAG hart=%u idx=%u prot=0x%lx base=0x%lx log2len=%lu\n",
                   hartid, i, prot, base, log2len);
    }
    sbi_printf("K3_PMP_DIAG hart=%u end=1\n", hartid);
}
#endif

#define BANNER                                              \
	"   ____                    _____ ____ _____\n"     \
	"  / __ \\                  / ____|  _ \\_   _|\n"  \
	" | |  | |_ __   ___ _ __ | (___ | |_) || |\n"      \
	" | |  | | '_ \\ / _ \\ '_ \\ \\___ \\|  _ < | |\n" \
	" | |__| | |_) |  __/ | | |____) | |_) || |_\n"     \
	"  \\____/| .__/ \\___|_| |_|_____/|____/_____|\n"  \
	"        | |\n"                                     \
	"        |_|\n\n"

static void sbi_boot_print_banner(struct sbi_scratch *scratch)
{
	if (scratch->options & SBI_SCRATCH_NO_BOOT_PRINTS)
		return;

#ifdef OPENSBI_VERSION_GIT
	sbi_printf("\nOpenSBI %s\n", OPENSBI_VERSION_GIT);
#else
	sbi_printf("\nOpenSBI v%d.%d\n", OPENSBI_VERSION_MAJOR,
		   OPENSBI_VERSION_MINOR);
#endif

#ifdef OPENSBI_BUILD_TIME_STAMP
	sbi_printf("Build time: %s\n", OPENSBI_BUILD_TIME_STAMP);
#endif

#ifdef OPENSBI_BUILD_COMPILER_VERSION
	sbi_printf("Build compiler: %s\n", OPENSBI_BUILD_COMPILER_VERSION);
#endif

	sbi_printf(BANNER);
}

static void sbi_boot_print_general(struct sbi_scratch *scratch)
{
	char str[128];
	const struct sbi_pmu_device *pdev;
	const struct sbi_hsm_device *hdev;
	const struct sbi_ipi_device *idev;
	const struct sbi_timer_device *tdev;
	const struct sbi_console_device *cdev;
	const struct sbi_system_reset_device *srdev;
	const struct sbi_system_suspend_device *susp_dev;
	const struct sbi_cppc_device *cppc_dev;
	const struct sbi_platform *plat = sbi_platform_ptr(scratch);

	if (scratch->options & SBI_SCRATCH_NO_BOOT_PRINTS)
		return;

	/* Platform details */
	sbi_printf("Platform Name               : %s\n",
		   sbi_platform_name(plat));
	sbi_platform_get_features_str(plat, str, sizeof(str));
	sbi_printf("Platform Features           : %s\n", str);
	sbi_printf("Platform HART Count         : %u\n",
		   sbi_platform_hart_count(plat));
	idev = sbi_ipi_get_device();
	sbi_printf("Platform IPI Device         : %s\n",
		   (idev) ? idev->name : "---");
	tdev = sbi_timer_get_device();
	sbi_printf("Platform Timer Device       : %s @ %luHz\n",
		   (tdev) ? tdev->name : "---",
		   (tdev) ? tdev->timer_freq : 0);
	cdev = sbi_console_get_device();
	sbi_printf("Platform Console Device     : %s\n",
		   (cdev) ? cdev->name : "---");
	hdev = sbi_hsm_get_device();
	sbi_printf("Platform HSM Device         : %s\n",
		   (hdev) ? hdev->name : "---");
	pdev = sbi_pmu_get_device();
	sbi_printf("Platform PMU Device         : %s\n",
		   (pdev) ? pdev->name : "---");
	srdev = sbi_system_reset_get_device(SBI_SRST_RESET_TYPE_COLD_REBOOT, 0);
	sbi_printf("Platform Reboot Device      : %s\n",
		   (srdev) ? srdev->name : "---");
	srdev = sbi_system_reset_get_device(SBI_SRST_RESET_TYPE_SHUTDOWN, 0);
	sbi_printf("Platform Shutdown Device    : %s\n",
		   (srdev) ? srdev->name : "---");
	susp_dev = sbi_system_suspend_get_device();
	sbi_printf("Platform Suspend Device     : %s\n",
		   (susp_dev) ? susp_dev->name : "---");
	cppc_dev = sbi_cppc_get_device();
	sbi_printf("Platform CPPC Device        : %s\n",
		   (cppc_dev) ? cppc_dev->name : "---");

	/* Firmware details */
	sbi_printf("Firmware Base               : 0x%lx\n", scratch->fw_start);
	sbi_printf("Firmware Size               : %d KB\n",
		   (u32)(scratch->fw_size / 1024));
	sbi_printf("Firmware RW Offset          : 0x%lx\n", scratch->fw_rw_offset);
	sbi_printf("Firmware RW Size            : %d KB\n",
		   (u32)((scratch->fw_size - scratch->fw_rw_offset) / 1024));
	sbi_printf("Firmware Heap Offset        : 0x%lx\n", scratch->fw_heap_offset);
	sbi_printf("Firmware Heap Size          : "
		   "%d KB (total), %d KB (reserved), %d KB (used), %d KB (free)\n",
		   (u32)(scratch->fw_heap_size / 1024),
		   (u32)(sbi_heap_reserved_space() / 1024),
		   (u32)(sbi_heap_used_space() / 1024),
		   (u32)(sbi_heap_free_space() / 1024));
	sbi_printf("Firmware Scratch Size       : "
		   "%d B (total), %d B (used), %d B (free)\n",
		   SBI_SCRATCH_SIZE,
		   (u32)sbi_scratch_used_space(),
		   (u32)(SBI_SCRATCH_SIZE - sbi_scratch_used_space()));

	/* SBI details */
	sbi_printf("Runtime SBI Version         : %d.%d\n",
		   sbi_ecall_version_major(), sbi_ecall_version_minor());
	sbi_ecall_get_extensions_str(str, sizeof(str), false);
	sbi_printf("Standard SBI Extensions     : %s\n", str);
	sbi_ecall_get_extensions_str(str, sizeof(str), true);
	sbi_printf("Experimental SBI Extensions : %s\n", str);
	sbi_printf("\n");
}

static void sbi_boot_print_domains(struct sbi_scratch *scratch)
{
	if (scratch->options & SBI_SCRATCH_NO_BOOT_PRINTS)
		return;

	/* Domain details */
	sbi_domain_dump_all("        ");
}

static void sbi_boot_print_hart(struct sbi_scratch *scratch, u32 hartid)
{
	int xlen;
	char str[128];
	const struct sbi_domain *dom = sbi_domain_thishart_ptr();

	if (scratch->options & SBI_SCRATCH_NO_BOOT_PRINTS)
		return;

	/* Determine MISA XLEN and MISA string */
	xlen = misa_xlen();
	if (xlen < 1) {
		sbi_printf("Error %d getting MISA XLEN\n", xlen);
		sbi_hart_hang();
	}

	/* Boot HART details */
	sbi_printf("Boot HART ID                : %u\n", hartid);
	sbi_printf("Boot HART Domain            : %s\n", dom->name);
	sbi_hart_get_priv_version_str(scratch, str, sizeof(str));
	sbi_printf("Boot HART Priv Version      : %s\n", str);
	misa_string(xlen, str, sizeof(str));
	sbi_printf("Boot HART Base ISA          : %s\n", str);
	sbi_hart_get_extensions_str(scratch, str, sizeof(str));
	sbi_printf("Boot HART ISA Extensions    : %s\n", str);
	sbi_printf("Boot HART PMP Count         : %d\n",
		   sbi_hart_pmp_count(scratch));
	sbi_printf("Boot HART PMP Granularity   : %u bits\n",
		   sbi_hart_pmp_log2gran(scratch));
	sbi_printf("Boot HART PMP Address Bits  : %d\n",
		   sbi_hart_pmp_addrbits(scratch));
	sbi_printf("Boot HART MHPM Info         : %lu (0x%08x)\n",
		   sbi_popcount(sbi_hart_mhpm_mask(scratch)),
		   sbi_hart_mhpm_mask(scratch));
	sbi_printf("Boot HART Debug Triggers    : %d triggers\n",
		   sbi_dbtr_get_total_triggers());
	sbi_hart_delegation_dump(scratch, "Boot HART ", "           ");
}

static unsigned long coldboot_done;

static void wait_for_coldboot(struct sbi_scratch *scratch)
{
	/* Wait for coldboot to finish */
	while (!__smp_load_acquire(&coldboot_done))
		cpu_relax();
}

static void wake_coldboot_harts(struct sbi_scratch *scratch)
{
	/* Mark coldboot done */
	__smp_store_release(&coldboot_done, 1);
}

static unsigned long entry_count_offset;
static unsigned long init_count_offset;

static void __noreturn init_coldboot(struct sbi_scratch *scratch, u32 hartid)
{
	int rc;
	unsigned long *count;
	const struct sbi_platform *plat = sbi_platform_ptr(scratch);

	/* Note: This has to be first thing in coldboot init sequence */
	rc = sbi_scratch_init(scratch);
	if (rc)
		sbi_hart_hang();

	/* Note: This has to be second thing in coldboot init sequence */
	rc = sbi_heap_init(scratch);
	if (rc)
		sbi_hart_hang();

	/* Note: This has to be the third thing in coldboot init sequence */
	rc = sbi_domain_init(scratch, hartid);
	if (rc)
		sbi_hart_hang();

	entry_count_offset = sbi_scratch_alloc_offset(__SIZEOF_POINTER__);
	if (!entry_count_offset)
		sbi_hart_hang();

	init_count_offset = sbi_scratch_alloc_offset(__SIZEOF_POINTER__);
	if (!init_count_offset)
		sbi_hart_hang();

	count = sbi_scratch_offset_ptr(scratch, entry_count_offset);
	(*count)++;

	rc = sbi_hsm_init(scratch, true);
	if (rc)
		sbi_hart_hang();

	/*
	 * All non-coldboot HARTs do HSM initialization (i.e. enter HSM state
	 * machine) at the start of the warmboot path so it is wasteful to
	 * have these HARTs busy spin in wait_for_coldboot() until coldboot
	 * path is completed.
	 */
	wake_coldboot_harts(scratch);

	rc = sbi_platform_early_init(plat, true);
	if (rc)
		sbi_hart_hang();

	rc = sbi_hart_init(scratch, true);
	if (rc)
		sbi_hart_hang();

	rc = sbi_sse_init(scratch, true);
	if (rc) {
		sbi_printf("%s: sse init failed (error %d)\n", __func__, rc);
		sbi_hart_hang();
	}

	rc = sbi_pmu_init(scratch, true);
	if (rc) {
		sbi_printf("%s: pmu init failed (error %d)\n",
			   __func__, rc);
		sbi_hart_hang();
	}

	rc = sbi_dbtr_init(scratch, true);
	if (rc)
		sbi_hart_hang();

	sbi_boot_print_banner(scratch);

	rc = sbi_irqchip_init(scratch, true);
	if (rc) {
		sbi_printf("%s: irqchip init failed (error %d)\n",
			   __func__, rc);
		sbi_hart_hang();
	}

	rc = sbi_ipi_init(scratch, true);
	if (rc) {
		sbi_printf("%s: ipi init failed (error %d)\n", __func__, rc);
		sbi_hart_hang();
	}

#ifdef CONFIG_K3_KEYSTONE_SM
	/* Register the existing Keystone PMP IPI event exactly once on cold
	 * boot, after the OpenSBI IPI core is ready and while all Hart scratch
	 * areas can still be allocated. The event is *not* a PMP admission:
	 * the post-PMP gate and SM init still fail closed until verified.
	 * Warm boot must reuse the registration rather than reallocating it.
	 */
	rc = k3_pmp_sync_initialize();
	if (rc) {
		sbi_printf("%s: K3 PMP IPI init failed (error %d)\n", __func__, rc);
		sbi_hart_hang();
	}
#endif

	rc = sbi_tlb_init(scratch, true);
	if (rc) {
		sbi_printf("%s: tlb init failed (error %d)\n", __func__, rc);
		sbi_hart_hang();
	}

	rc = sbi_timer_init(scratch, true);
	if (rc) {
		sbi_printf("%s: timer init failed (error %d)\n", __func__, rc);
		sbi_hart_hang();
	}

	rc = sbi_fwft_init(scratch, true);
	if (rc) {
		sbi_printf("%s: fwft init failed (error %d)\n", __func__, rc);
		sbi_hart_hang();
	}

	/*
	 * Note: Finalize domains after HSM initialization so that we
	 * can startup non-root domains.
	 * Note: Finalize domains before HART PMP configuration so
	 * that we use correct domain for configuring PMP.
	 */
	rc = sbi_domain_finalize(scratch, hartid);
	if (rc) {
		sbi_printf("%s: domain finalize failed (error %d)\n",
			   __func__, rc);
		sbi_hart_hang();
	}

	/*
	 * Note: Each supervisor domain will need its own MPXY shared
	 * memory region.
	 * Note: Initialize the MPXY after sbi_domain_finalize().
	*/
	rc = sbi_mpxy_init(scratch);
	if (rc) {
		sbi_printf("%s: mpxy init failed (error %d)\n", __func__, rc);
		sbi_hart_hang();
	}

	/*
	 * Note: Platform final initialization should be after finalizing
	 * domains so that it sees correct domain assignment and PMP
	 * configuration for FDT fixups.
	 */
	rc = sbi_platform_final_init(plat, true);
	if (rc) {
		sbi_printf("%s: platform final init failed (error %d)\n",
			   __func__, rc);
		sbi_hart_hang();
	}

	/*
	 * Note: Ecall initialization should be after platform final
	 * initialization so that all available platform devices are
	 * already registered.
	 */
	rc = sbi_ecall_init();
	if (rc) {
		sbi_printf("%s: ecall init failed (error %d)\n", __func__, rc);
		sbi_hart_hang();
	}

	sbi_boot_print_general(scratch);

	sbi_boot_print_domains(scratch);

	sbi_boot_print_hart(scratch, hartid);

	run_all_tests();

	/*
	 * Configure PMP at last because if SMEPMP is detected,
	 * M-mode access to the S/U space will be rescinded.
	 */
#if defined(CONFIG_K3_KEYSTONE_SM) && defined(K3_PMP_BOOT_TRACE) && K3_PMP_BOOT_TRACE
    sbi_printf("[K3-PMP] init_coldboot before configure hart=%u\n", hartid);
#endif
	rc = sbi_hart_pmp_configure(scratch);
#if defined(CONFIG_K3_KEYSTONE_SM) && defined(K3_PMP_BOOT_TRACE) && K3_PMP_BOOT_TRACE
    sbi_printf("[K3-PMP] init_coldboot after configure hart=%u rc=%d\n", hartid, rc);
#endif
	if (rc) {
		sbi_printf("%s: PMP configure failed (error %d)\n",
			   __func__, rc);
		sbi_hart_hang();
	}

#if defined(K3_PMP_DIAG_READONLY) && defined(CONFIG_PLATFORM_SPACEMIT_K3) && !defined(CONFIG_K3_KEYSTONE_SM)
	k3_pmp_diag_readonly(scratch, hartid);
#endif

#ifdef CONFIG_K3_KEYSTONE_SM
	/* A1-E MUST run after local root-domain PMP initialization, while
	 * still in M-mode and before the HSM path switches into S/U-mode.
	 * This gate is currently fail-closed until the global proof exists.
	 */
	k3_keystone_a1_post_pmp_before_smode(scratch, hartid);
	rc = sm_init(true);
	if (rc) {
		sbi_printf("K3 Keystone: sm_init unavailable (error %d); S-mode admission denied\n", rc);
		sbi_hart_hang();
	}
	/* Only the initial HSM Start window is opened here. At this point
	 * dynamic enclave updates remain unavailable because all 16 Hart
	 * HSM states have not yet reached STARTED. No new IPI barrier. */
	rc = k3_keystone_allow_initial_hsm_starts();
	if (rc) {
		sbi_printf("K3 Keystone: initial HSM Start admission failed (%d)\n", rc);
		sbi_hart_hang();
	}
#endif

	count = sbi_scratch_offset_ptr(scratch, init_count_offset);
	(*count)++;

	sbi_hsm_hart_start_finish(scratch, hartid);
}

static void __noreturn init_warm_startup(struct sbi_scratch *scratch,
					 u32 hartid)
{
	int rc;
	unsigned long *count;
	const struct sbi_platform *plat = sbi_platform_ptr(scratch);

	if (!entry_count_offset || !init_count_offset)
		sbi_hart_hang();

	count = sbi_scratch_offset_ptr(scratch, entry_count_offset);
	(*count)++;

	/* Note: This has to be first thing in warmboot init sequence */
	rc = sbi_hsm_init(scratch, false);
	if (rc)
		sbi_hart_hang();

	rc = sbi_platform_early_init(plat, false);
	if (rc)
		sbi_hart_hang();

	rc = sbi_hart_init(scratch, false);
	if (rc)
		sbi_hart_hang();

	rc = sbi_sse_init(scratch, false);
	if (rc)
		sbi_hart_hang();

	rc = sbi_pmu_init(scratch, false);
	if (rc)
		sbi_hart_hang();

	rc = sbi_dbtr_init(scratch, false);
	if (rc)
		sbi_hart_hang();

	rc = sbi_irqchip_init(scratch, false);
	if (rc)
		sbi_hart_hang();

	rc = sbi_ipi_init(scratch, false);
	if (rc)
		sbi_hart_hang();

	rc = sbi_tlb_init(scratch, false);
	if (rc)
		sbi_hart_hang();

	rc = sbi_timer_init(scratch, false);
	if (rc)
		sbi_hart_hang();

	rc = sbi_fwft_init(scratch, false);
	if (rc)
		sbi_hart_hang();

	rc = sbi_platform_final_init(plat, false);
	if (rc)
		sbi_hart_hang();

	/*
	 * Configure PMP at last because if SMEPMP is detected,
	 * M-mode access to the S/U space will be rescinded.
	 */
	rc = sbi_hart_pmp_configure(scratch);
	if (rc)
		sbi_hart_hang();

#if defined(K3_PMP_DIAG_READONLY) && defined(CONFIG_PLATFORM_SPACEMIT_K3) && !defined(CONFIG_K3_KEYSTONE_SM)
	k3_pmp_diag_readonly(scratch, hartid);
#endif

#ifdef CONFIG_K3_KEYSTONE_SM
	/* A1-E MUST run after local root-domain PMP initialization, while
	 * still in M-mode and before the HSM path switches into S/U-mode.
	 * This gate is currently fail-closed until the global proof exists.
	 */
	k3_keystone_a1_post_pmp_before_smode(scratch, hartid);
	rc = sm_init(false);
	if (rc) {
		sbi_printf("K3 Keystone: warm sm_init unavailable (error %d); S-mode admission denied\n", rc);
		sbi_hart_hang();
	}
#endif

	count = sbi_scratch_offset_ptr(scratch, init_count_offset);
	(*count)++;

	sbi_hsm_hart_start_finish(scratch, hartid);
}

static void __noreturn init_warm_resume(struct sbi_scratch *scratch,
					u32 hartid)
{
	int rc;

	sbi_hsm_hart_resume_start(scratch);

	rc = sbi_hart_reinit(scratch);
	if (rc)
		sbi_hart_hang();

	rc = sbi_hart_pmp_configure(scratch);
	if (rc)
		sbi_hart_hang();

#if defined(K3_PMP_DIAG_READONLY) && defined(CONFIG_PLATFORM_SPACEMIT_K3) && !defined(CONFIG_K3_KEYSTONE_SM)
	k3_pmp_diag_readonly(scratch, hartid);
#endif

#ifdef CONFIG_K3_KEYSTONE_SM
	/* Never resume S-mode with a fresh unverified PMP layout. */
	k3_keystone_a1_post_pmp_before_smode(scratch, hartid);
#endif

	sbi_hsm_hart_resume_finish(scratch, hartid);
}

static void __noreturn init_warmboot(struct sbi_scratch *scratch, u32 hartid)
{
	int hstate;

	wait_for_coldboot(scratch);

	hstate = sbi_hsm_hart_get_state(sbi_domain_thishart_ptr(), hartid);
	if (hstate < 0)
		sbi_hart_hang();

	if (hstate == SBI_HSM_STATE_SUSPENDED) {
		init_warm_resume(scratch, hartid);
	} else {
		sbi_ipi_raw_clear();
		init_warm_startup(scratch, hartid);
	}
}

static atomic_t coldboot_lottery = ATOMIC_INITIALIZER(0);

/**
 * Initialize OpenSBI library for current HART and jump to next
 * booting stage.
 *
 * The function expects following:
 * 1. The 'mscratch' CSR is pointing to sbi_scratch of current HART
 * 2. Stack pointer (SP) is setup for current HART
 * 3. Interrupts are disabled in MSTATUS CSR
 * 4. All interrupts are disabled in MIE CSR
 *
 * @param scratch pointer to sbi_scratch of current HART
 */
void __noreturn sbi_init(struct sbi_scratch *scratch)
{
	bool next_mode_supported	= false;
	bool coldboot			= false;
	u32 hartid			= current_hartid();
	const struct sbi_platform *plat = sbi_platform_ptr(scratch);

	switch (scratch->next_mode) {
	case PRV_M:
		next_mode_supported = true;
		break;
	case PRV_S:
		if (misa_extension('S'))
			next_mode_supported = true;
		break;
	case PRV_U:
		if (misa_extension('U'))
			next_mode_supported = true;
		break;
	default:
		sbi_hart_hang();
	}

	/*
	 * Only the HART supporting privilege mode specified in the
	 * scratch->next_mode should be allowed to become the coldboot
	 * HART because the coldboot HART will be directly jumping to
	 * the next booting stage.
	 *
	 * We use a lottery mechanism to select coldboot HART among
	 * HARTs which satisfy above condition.
	 */

	if (sbi_platform_cold_boot_allowed(plat, hartid)) {
		if (next_mode_supported &&
		    atomic_xchg(&coldboot_lottery, 1) == 0)
			coldboot = true;
	}

	/*
	 * Do platform specific nascent (very early) initialization so
	 * that platform can initialize platform specific per-HART CSRs
	 * or per-HART devices.
	 */
	if (sbi_platform_nascent_init(plat))
		sbi_hart_hang();

	if (coldboot)
		init_coldboot(scratch, hartid);
	else
		init_warmboot(scratch, hartid);
}

unsigned long sbi_entry_count(u32 hartindex)
{
	struct sbi_scratch *scratch;
	unsigned long *entry_count;

	if (!entry_count_offset)
		return 0;

	scratch = sbi_hartindex_to_scratch(hartindex);
	if (!scratch)
		return 0;

	entry_count = sbi_scratch_offset_ptr(scratch, entry_count_offset);

	return *entry_count;
}

unsigned long sbi_init_count(u32 hartindex)
{
	struct sbi_scratch *scratch;
	unsigned long *init_count;

	if (!init_count_offset)
		return 0;

	scratch = sbi_hartindex_to_scratch(hartindex);
	if (!scratch)
		return 0;

	init_count = sbi_scratch_offset_ptr(scratch, init_count_offset);

	return *init_count;
}

/**
 * Exit OpenSBI library for current HART and stop HART
 *
 * The function expects following:
 * 1. The 'mscratch' CSR is pointing to sbi_scratch of current HART
 * 2. Stack pointer (SP) is setup for current HART
 *
 * @param scratch pointer to sbi_scratch of current HART
 */
void __noreturn sbi_exit(struct sbi_scratch *scratch)
{
	u32 hartid			= current_hartid();
	const struct sbi_platform *plat = sbi_platform_ptr(scratch);

	if (!sbi_hartid_valid(hartid))
		sbi_hart_hang();

	sbi_platform_early_exit(plat);

	sbi_sse_exit(scratch);

	sbi_pmu_exit(scratch);

	sbi_timer_exit(scratch);

	sbi_ipi_exit(scratch);

	sbi_irqchip_exit(scratch);

	sbi_platform_final_exit(plat);

	sbi_hsm_exit(scratch);
}
