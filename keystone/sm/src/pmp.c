//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include "sm_assert.h"
#include "pmp.h"
#include "cpu.h"
#include "safe_math_util.h"
#include "sm-sbi-opensbi.h"
#include "page.h"
#include "ipi.h"
#include <sbi/sbi_hart.h>
#include <sbi/riscv_asm.h>
#include <sbi/riscv_locks.h>
#include <sbi/riscv_atomic.h>
#ifdef KEYSTONE_K3_PMP_N_REG
#include <sbi/k3_pmp_partition_evidence.h>
#endif

#ifndef TARGET_PLATFORM_HEADER
#error "SM requires a defined platform to build"
#endif

// Special target platform header, set by configure script
#include TARGET_PLATFORM_HEADER

/* PMP global spin locks */
static spinlock_t pmp_lock = SPIN_LOCK_INITIALIZER;

/* PMP region getter/setters */
static struct pmp_region regions[PMP_MAX_N_REGION];
static uint32_t reg_bitmap = 0;
static uint32_t region_def_bitmap = 0;
#ifdef KEYSTONE_K3_PMP_N_REG
/* Default deny: no PMP entry is writable by Keystone without a verified,
 * identical, all-Hart partition. This DOES NOT complete the ownership handoff. */
static uint16_t k3_pmp_allowed;
static int k3_pmp_partition_prepared;
/* Read-only readiness check; a candidate PMP shape is not an ownership
 * commitment. The boot path must call the verified partition preparer. */
int k3_pmp_partition_is_prepared(void)
{
  return __atomic_load_n(&k3_pmp_partition_prepared, __ATOMIC_ACQUIRE) != 0;
}

int k3_pmp_uniform_partition_prepare(const struct k3_pmp_partition_evidence *ev)
{
  uint16_t mask = 0;
  uint32_t valid = (1U << PMP_N_REG) - 1U;
  if (k3_pmp_partition_prepared || region_def_bitmap || reg_bitmap)
    return -1;
  if (k3_pmp_validate_uniform_partition(ev, &mask)) return -1;
  if (mask & ~valid) return -1;
  k3_pmp_allowed = mask;
  reg_bitmap = valid & ~((uint32_t)mask);
  k3_pmp_partition_prepared = 1;
  return 0;
}

/* QEMU-only A1-E cold-boot partition: local root-domain CSR proof is
 * supplied by the OpenSBI pre-S-mode gate, not synthetic 16-Hart evidence.
 * Each secondary Hart must independently verify the same layout before its
 * first S-mode entry. No dynamic enclave PMP change is admitted here. */
int k3_pmp_boot_partition_admit(uint16_t opensbi_mask, uint16_t keystone_mask,
                                 int cold_boot)
{
  const uint16_t full = 0xffffU;
  if (!opensbi_mask || !keystone_mask ||
      (opensbi_mask & keystone_mask) ||
      (uint16_t)(opensbi_mask | keystone_mask) != full ||
      !(opensbi_mask & 0x8000U) || (keystone_mask & 0x8000U) ||
      __builtin_popcount((unsigned int)keystone_mask) < 3)
    return -1;
  if (!cold_boot)
    return k3_pmp_partition_prepared &&
           k3_pmp_allowed == keystone_mask ? 0 : -1;
  if (k3_pmp_partition_prepared || region_def_bitmap || reg_bitmap)
    return -1;
  k3_pmp_allowed = keystone_mask;
  reg_bitmap = full & ~((uint32_t)keystone_mask);
  __atomic_store_n(&k3_pmp_partition_prepared, 1, __ATOMIC_RELEASE);
  return 0;
}

static int k3_pmp_index_allowed(int n)
{
  return k3_pmp_partition_prepared && n >= 0 && n < PMP_N_REG &&
         ((k3_pmp_allowed & (1U << n)) != 0);
}
static int k3_pmp_extreme_allowed(int highest)
{
  int i;
  if (!k3_pmp_partition_prepared) return -1;
  if (highest) {
    for (i = PMP_N_REG - 1; i >= 0; --i)
      if (k3_pmp_index_allowed(i)) return i;
  } else {
    for (i = 0; i < PMP_N_REG; ++i)
      if (k3_pmp_index_allowed(i)) return i;
  }
  return -1;
}
#endif

static inline int region_register_idx(region_id i)
{
  return regions[i].reg_idx;
}

static inline int region_allows_overlap(region_id i)
{
  return regions[i].allow_overlap;
}

static inline uintptr_t region_get_addr(region_id i)
{
  return regions[i].addr;
}

static inline uint64_t region_get_size(region_id i)
{
  return regions[i].size;
}

static inline int region_is_napot(region_id i)
{
  return regions[i].addrmode == PMP_A_NAPOT;
}

static inline int region_is_tor(region_id i)
{
  return regions[i].addrmode == PMP_A_TOR;
}

static inline int region_needs_two_entries(region_id i)
{
  return region_is_tor(i) && regions[i].reg_idx > 0;
}

static inline int region_is_napot_all(region_id i)
{
  return regions[i].addr == 0 && regions[i].size == -1UL;
}

static inline uintptr_t region_pmpaddr_val(region_id i)
{
  if(region_is_napot_all(i))
    return (-1UL);
  else if(region_is_napot(i))
    return (regions[i].addr | (regions[i].size/2-1)) >> 2;
  else if(region_is_tor(i))
    return (regions[i].addr + regions[i].size) >> 2;
  else
    return 0;
}

static inline uintptr_t region_pmpcfg_val(region_id i, pmpreg_id reg_idx, uint8_t perm_bits)
{
  return (uintptr_t) (regions[i].addrmode | perm_bits) << (8*(reg_idx%PMP_PER_GROUP));
}

static void region_clear_all(region_id i)
{
  regions[i].addr = 0;
  regions[i].size = 0;
  regions[i].addrmode = 0;
  regions[i].allow_overlap = 0;
  regions[i].reg_idx = 0;
}

static void region_init(region_id i,
                        uintptr_t addr,
                        uint64_t size,
                        uint8_t addrmode,
                        int allow_overlap,
                        pmpreg_id reg_idx)
{
  regions[i].addr = addr;
  regions[i].size = size;
  regions[i].addrmode = addrmode;
  regions[i].allow_overlap = allow_overlap;
  regions[i].reg_idx = (addrmode == PMP_A_TOR && reg_idx > 0 ? reg_idx + 1 : reg_idx);
}

static int is_pmp_region_valid(region_id region_idx)
{
  return TEST_BIT(region_def_bitmap, region_idx);
}

static int search_rightmost_unset(uint32_t bitmap, int max, uint32_t mask)
{
  int i = 0;

  sm_assert(max < 32);
  sm_assert(!((mask + 1) & mask));

  while(mask < (1UL << max)) {
    if((~bitmap & mask) == mask)
      return i;
    mask = mask << 1;
    i++;
  }

  return -1;
}

static region_id get_free_region_idx(void)
{
  return search_rightmost_unset(region_def_bitmap, PMP_MAX_N_REGION, 0x1);
}

static pmpreg_id get_free_reg_idx(void)
{
#ifdef KEYSTONE_K3_PMP_N_REG
  if (!k3_pmp_partition_prepared) return -1;
#endif
  return search_rightmost_unset(reg_bitmap, PMP_N_REG, 0x1);
}

static pmpreg_id get_conseq_free_reg_idx(void)
{
#ifdef KEYSTONE_K3_PMP_N_REG
  if (!k3_pmp_partition_prepared) return -1;
#endif
  return search_rightmost_unset(reg_bitmap, PMP_N_REG, 0x3);
}

/* We do an integery overflow safety check here for the inputs (addr +
 * size).  We do NOT do a safety check on epm_base + epm_size, since
 * only valid region should have been created previously.
 *
 * On a failed addr + size overflow, we return failure, since this
 * cannot be a valid addr and size anyway.
 */
static int detect_region_overlap(uintptr_t addr, uintptr_t size)
{
  void* epm_base;
  size_t epm_size;
  int region_overlap = 0, i;

  // Safety check the addr+size
  uintptr_t input_end;
  if( CHECKED_ADD(addr, size, &input_end)){
    return 1;
  }

  for(i=0; i<PMP_MAX_N_REGION; i++)
  {
    if(!is_pmp_region_valid(i))
      continue;

    if(region_allows_overlap(i)) {
      continue;
    }

    epm_base = (void*) region_get_addr(i);
    epm_size = region_get_size(i);

    // Only looking at valid regions, no need to check epm_base+size
    region_overlap |= ((uintptr_t) epm_base < input_end) &&
                      ((uintptr_t) epm_base + epm_size > addr);
  }

  return region_overlap;
}

int pmp_detect_region_overlap_atomic(uintptr_t addr, uintptr_t size)
{
  int region_overlap = 0;
  spin_lock(&pmp_lock);
  region_overlap = detect_region_overlap(addr, size);
  spin_unlock(&pmp_lock);
  return region_overlap;
}

/*********************************
 *
 * External Functions
 *
 **********************************/

int pmp_unset_global(int region_idx)
{
  int rc;
  if (!is_pmp_region_valid(region_idx))
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_INVALID, "Invalid PMP region index");
  if (k3_keystone_pmp_transition_begin())
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_INVALID, "HSM Start in progress");
  rc = send_and_sync_pmp_ipi(region_idx, SBI_PMP_IPI_TYPE_UNSET,
                             PMP_NO_PERM);
  k3_keystone_pmp_transition_finish();
  if (rc)
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_INVALID,
              "PMP global unset unavailable or incomplete");
  return SBI_ERR_SM_PMP_SUCCESS;
}

int pmp_set_global(int region_idx, uint8_t perm)
{
  int rc;
  if (!is_pmp_region_valid(region_idx))
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_INVALID, "Invalid PMP region index");
  if (k3_keystone_pmp_transition_begin())
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_INVALID, "HSM Start in progress");
  rc = send_and_sync_pmp_ipi(region_idx, SBI_PMP_IPI_TYPE_SET, perm);
  k3_keystone_pmp_transition_finish();
  if (rc)
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_INVALID,
              "PMP global set unavailable or incomplete");
  return SBI_ERR_SM_PMP_SUCCESS;
}

int pmp_init(void)
{
#ifdef KEYSTONE_K3_PMP_N_REG
  if (!k3_pmp_partition_prepared) {
    sbi_printf("[SM/K3] PMP partition not verified\n");
    return SBI_ERR_SM_NOT_IMPLEMENTED;
  }
#endif
  uintptr_t pmpaddr = 0;
  uintptr_t pmpcfg = 0;
  int i;
  for (i=0; i < PMP_N_REG; i++)
  {
#ifdef KEYSTONE_K3_PMP_N_REG
    /* Preserve every OpenSBI-owned PMP entry and its locked state. */
    if (!k3_pmp_index_allowed(i)) continue;
#endif
    switch(i) {
#define X(n,g) case n: { PMP_SET(n, g, pmpaddr, pmpcfg); break; }
      LIST_OF_PMP_REGS
#undef X
    }
  }
  return SBI_ERR_SM_PMP_SUCCESS;
}

int pmp_set_keystone(int region_idx, uint8_t perm)
{
  if(!is_pmp_region_valid(region_idx))
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_INVALID, "Invalid PMP region index");

  uint8_t perm_bits = perm & PMP_ALL_PERM;
  pmpreg_id reg_idx = region_register_idx(region_idx);
#ifdef KEYSTONE_K3_PMP_N_REG
  if (!k3_pmp_index_allowed(reg_idx) ||
      (region_needs_two_entries(region_idx) && !k3_pmp_index_allowed(reg_idx-1)))
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_INVALID, "PMP slot not Keystone-owned");
#endif
  uintptr_t pmpcfg = region_pmpcfg_val(region_idx, reg_idx, perm_bits);
  uintptr_t pmpaddr;

  pmpaddr = region_pmpaddr_val(region_idx);

  //sbi_printf("pmp_set() [hart %d]: reg[%d], mode[%s], range[0x%lx-0x%lx], perm[0x%x]\r\n",
  //       current_hartid(), reg_idx, (region_is_tor(region_idx) ? "TOR":"NAPOT"),
  //       region_get_addr(region_idx), region_get_addr(region_idx) + region_get_size(region_idx), perm);
  //sbi_printf("  pmp[%d] = pmpaddr: 0x%lx, pmpcfg: 0x%lx\r\n", reg_idx, pmpaddr, pmpcfg);

  int n=reg_idx;

  switch(n) {
#define X(n,g) case n: { PMP_SET(n, g, pmpaddr, pmpcfg); break; }
  LIST_OF_PMP_REGS
#undef X
    default:
      sm_assert(false);
  }

  /* TOR decoding with 2 registers */
  if(region_needs_two_entries(region_idx))
  {
    n--;
    pmpcfg = 0;
    pmpaddr = region_get_addr(region_idx) >> 2;
    switch(n) {
#define X(n,g) case n: { PMP_SET(n, g, pmpaddr, pmpcfg); break; }
  LIST_OF_PMP_REGS
#undef X
    default:
      sm_assert(false);
    }
  }
  return SBI_ERR_SM_PMP_SUCCESS;
}

int pmp_unset(int region_idx);

/* Read raw entries: pmp_get() represents size as an order and cannot
 * describe an arbitrary TOR interval. Boundary addresses here are pmpaddr
 * units (four bytes), including the WARL low bits for the current mode.
 */
static int k3_pmp_read_entry(unsigned int reg, unsigned long *flags,
			     unsigned long *address)
{
    unsigned long cfg;

    switch (reg) {
#define X(n, g)                                                                \
    case n:                                                                    \
	cfg = csr_read(pmpcfg##g);                                             \
	*address = csr_read(pmpaddr##n);                                       \
	break;
	LIST_OF_PMP_REGS
#undef X
    default:
	return -1;
    }
    *flags = (cfg >> (8 * (reg % PMP_PER_GROUP))) & 0xff;
    return 0;
}

static int k3_pmp_verify_tor(int region, int operation, uint8_t permissions)
{
    unsigned int reg;
    unsigned long flags;
    unsigned long address;
    unsigned long lower_flags;
    unsigned long lower_address;
    uintptr_t start;
    uint64_t size;

    reg = region_register_idx(region);
    if (k3_pmp_read_entry(reg, &flags, &address)) {
	return -1;
    }
    lower_address = 0;
    if (reg) {
	if (k3_pmp_read_entry(reg - 1, &lower_flags, &lower_address)) {
	    return -1;
	}
	/* The lower entry supplies the boundary and must remain OFF,
	 * unlocked and without permissions of its own.
	 */
	if (lower_flags != 0) {
	    return -1;
	}
    }
    if (operation == SBI_PMP_IPI_TYPE_UNSET) {
	return flags == 0 ? 0 : -1;
    }
    if (flags != (PMP_A_TOR | (permissions & PMP_ALL_PERM))) {
	return -1;
    }
    start = region_get_addr(region);
    size = region_get_size(region);
    if (!size || start + size < start) {
	return -1;
    }
    if (lower_address != (start >> 2) || address != ((start + size) >> 2)) {
	return -1;
    }
    return 0;
}

/* Called on each receiving hart, including the sender. A successful return
 * requires local CSR readback; the sender also requires every generation ACK.
 */
int k3_pmp_apply_verified(int region, int operation, uint8_t permissions)
{
    unsigned long flags;
    unsigned long base;
    unsigned long order;
    unsigned int reg;
    int rc;

    if (region < 0 || region >= PMP_MAX_N_REGION ||
	!is_pmp_region_valid(region)) {
	return -1;
    }
    reg = region_register_idx(region);
    if (operation == SBI_PMP_IPI_TYPE_SET) {
	rc = pmp_set_keystone(region, permissions);
    } else if (operation == SBI_PMP_IPI_TYPE_UNSET) {
	rc = pmp_unset(region);
    } else {
	return -1;
    }
    if (rc != SBI_ERR_SM_PMP_SUCCESS) {
	return -1;
    }
    /* Flush cached translations before acknowledging the new permissions. */
    asm volatile("fence iorw, iorw\n\tsfence.vma" ::: "memory");
    if (region_is_tor(region)) {
	return k3_pmp_verify_tor(region, operation, permissions);
    }
    rc = pmp_get(reg, &flags, &base, &order);
    if (rc) {
	return -1;
    }
    if (operation == SBI_PMP_IPI_TYPE_UNSET) {
	return (flags & PMP_A) ? -1 : 0;
    }
    if ((flags & (PMP_A | PMP_ALL_PERM)) !=
	(regions[region].addrmode | (permissions & PMP_ALL_PERM))) {
	return -1;
    }
    /* Compare effective ranges, not raw WARL NAPOT encodings. */
    if (region_is_napot_all(region)) {
	return (base == 0 && order == __riscv_xlen) ? 0 : -1;
    }
    if (base != region_get_addr(region) || order >= sizeof(unsigned long) * 8) {
	return -1;
    }
    if ((1UL << order) != region_get_size(region)) {
	return -1;
    }
    return 0;
}

int pmp_unset(int region_idx)
{
  if(!is_pmp_region_valid(region_idx))
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_INVALID,"Invalid PMP region index");

  pmpreg_id reg_idx = region_register_idx(region_idx);
#ifdef KEYSTONE_K3_PMP_N_REG
  if (!k3_pmp_index_allowed(reg_idx) ||
      (region_needs_two_entries(region_idx) && !k3_pmp_index_allowed(reg_idx-1)))
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_INVALID, "PMP slot not Keystone-owned");
#endif
  int n=reg_idx;
  switch(n) {
#define X(n,g) case n: { PMP_UNSET(n, g); break;}
  LIST_OF_PMP_REGS
#undef X
    default:
      sm_assert(false);
  }

  if(region_needs_two_entries(region_idx))
  {
    n--;
    switch(n) {
#define X(n,g) case n: { PMP_UNSET(n,g); break; }
  LIST_OF_PMP_REGS
#undef X
    default:
      sm_assert(false);
    }
  }

  return SBI_ERR_SM_PMP_SUCCESS;
}

int pmp_region_init_atomic(uintptr_t start, uint64_t size, enum pmp_priority priority, region_id* rid, int allow_overlap)
{
  int ret;
#ifdef KEYSTONE_K3_PMP_N_REG
  if (!k3_pmp_partition_prepared)
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_INVALID, "No verified PMP partition");
#endif
  /* ADR-001: region allocation alters the protection policy even before
   * the associated CSR write. Do not admit HSM Start concurrently. */
  if (k3_keystone_pmp_assert_active())
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_INVALID, "HSM Start in progress");
  spin_lock(&pmp_lock);
  ret = pmp_region_init(start, size, priority, rid, allow_overlap);
  spin_unlock(&pmp_lock);
  return ret;
}

static int tor_region_init(uintptr_t start, uint64_t size, enum pmp_priority priority, region_id* rid, int allow_overlap)
{
  pmpreg_id reg_idx = -1;
  region_id region_idx = -1;

  sm_assert(size);
  sm_assert(!(size & (RISCV_PGSIZE-1)));
  sm_assert(!(start & (RISCV_PGSIZE-1)));
  sm_assert(rid);
  sm_assert(priority != PMP_PRI_BOTTOM);

  region_idx = get_free_region_idx();
  if(region_idx < 0 || region_idx > PMP_MAX_N_REGION)
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_MAX_REACHED, "Reached the maximum number of PMP regions");

  *rid = region_idx;
  switch(priority)
  {
    case(PMP_PRI_ANY): {
      reg_idx = get_conseq_free_reg_idx();
      if(reg_idx < 0)
        PMP_ERROR(SBI_ERR_SM_PMP_REGION_MAX_REACHED, "No available PMP register");
      if(TEST_BIT(reg_bitmap, reg_idx) || TEST_BIT(reg_bitmap, reg_idx + 1) || reg_idx + 1 >= PMP_N_REG)
        PMP_ERROR(SBI_ERR_SM_PMP_REGION_MAX_REACHED, "PMP register unavailable");

      break;
    }
    case(PMP_PRI_TOP): {
      sm_assert(start == 0);
#ifdef KEYSTONE_K3_PMP_N_REG
      reg_idx = k3_pmp_extreme_allowed(0);
      if (reg_idx < 0)
        PMP_ERROR(SBI_ERR_SM_PMP_REGION_MAX_REACHED, "No verified top slot");
#else
      reg_idx = 0;
#endif
      if(TEST_BIT(reg_bitmap, reg_idx))
        PMP_ERROR(SBI_ERR_SM_PMP_REGION_MAX_REACHED, "PMP register unavailable");
      break;
    }
    default: {
      sm_assert(0);
    }
  }

  // initialize the region
  region_init(region_idx, start, size, PMP_A_TOR, allow_overlap, reg_idx);
  SET_BIT(region_def_bitmap, region_idx);
  SET_BIT(reg_bitmap, reg_idx);

  if(reg_idx > 0)
    SET_BIT(reg_bitmap, reg_idx + 1);

  return SBI_ERR_SM_PMP_SUCCESS;
}

static int napot_region_init(uintptr_t start, uint64_t size, enum pmp_priority priority, region_id* rid, int allow_overlap)
{
  pmpreg_id reg_idx = -1;
  region_id region_idx = -1;

  sm_assert(size);
  sm_assert(rid);

  if(!(size == -1UL && start == 0))
  {
    sm_assert(!(size & (size-1)));
    sm_assert(!(start & (size - 1)));
    sm_assert(!(size & (RISCV_PGSIZE-1)));
    sm_assert(!(start & (RISCV_PGSIZE-1)));
  }

  //find avaiable pmp region idx
  region_idx = get_free_region_idx();
  if(region_idx < 0 || region_idx > PMP_MAX_N_REGION)
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_MAX_REACHED, "Reached the maximum number of PMP regions");

  *rid = region_idx;

  switch(priority)
  {
    case(PMP_PRI_ANY): {
      reg_idx = get_free_reg_idx();
      if(reg_idx < 0)
        PMP_ERROR(SBI_ERR_SM_PMP_REGION_MAX_REACHED, "No available PMP register");
      if(TEST_BIT(reg_bitmap, reg_idx) || reg_idx >= PMP_N_REG)
        PMP_ERROR(SBI_ERR_SM_PMP_REGION_MAX_REACHED, "PMP register unavailable");
      break;
    }
    case(PMP_PRI_TOP): {
#ifdef KEYSTONE_K3_PMP_N_REG
      reg_idx = k3_pmp_extreme_allowed(0);
      if (reg_idx < 0)
        PMP_ERROR(SBI_ERR_SM_PMP_REGION_MAX_REACHED, "No verified top slot");
#else
      reg_idx = 0;
#endif
      if(TEST_BIT(reg_bitmap, reg_idx))
        PMP_ERROR(SBI_ERR_SM_PMP_REGION_MAX_REACHED, "PMP register unavailable");
      break;
    }
    case(PMP_PRI_BOTTOM): {
      /* the bottom register can be used by multiple regions,
       * so we don't check its availability */
#ifdef KEYSTONE_K3_PMP_N_REG
      reg_idx = k3_pmp_extreme_allowed(1);
      if (reg_idx < 0)
        PMP_ERROR(SBI_ERR_SM_PMP_REGION_MAX_REACHED, "No verified bottom slot");
#else
      reg_idx = PMP_N_REG - 1;
#endif
      break;
    }
    default: {
      sm_assert(0);
    }
  }

  // initialize the region
  region_init(region_idx, start, size, PMP_A_NAPOT, allow_overlap, reg_idx);
  SET_BIT(region_def_bitmap, region_idx);
  SET_BIT(reg_bitmap, reg_idx);

  return SBI_ERR_SM_PMP_SUCCESS;
}

int pmp_region_free_atomic(int region_idx)
{
  /* Releasing the last Enclave region does NOT prove the PMP is INACTIVE.
   * Only a separately verified complete protection teardown may do so. */
  if (k3_keystone_pmp_assert_active())
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_INVALID, "HSM Start in progress");

  spin_lock(&pmp_lock);

  if(!is_pmp_region_valid(region_idx))
  {
    spin_unlock(&pmp_lock);
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_INVALID, "Invalid PMP region index");
  }

  pmpreg_id reg_idx = region_register_idx(region_idx);
  UNSET_BIT(region_def_bitmap, region_idx);
  UNSET_BIT(reg_bitmap, reg_idx);
  if(region_needs_two_entries(region_idx))
    UNSET_BIT(reg_bitmap, reg_idx - 1);

  region_clear_all(region_idx);

  spin_unlock(&pmp_lock);

  return SBI_ERR_SM_PMP_SUCCESS;
}

int pmp_region_init(uintptr_t start, uint64_t size, enum pmp_priority priority, int* rid, int allow_overlap)
{
#ifdef KEYSTONE_K3_PMP_N_REG
  if (!k3_pmp_partition_prepared)
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_INVALID, "No verified PMP partition");
#endif
  if(!size)
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_SIZE_INVALID, "Invalid PMP size");

  /* overlap detection */
  if (!allow_overlap) {
    if (detect_region_overlap(start, size)) {
      return SBI_ERR_SM_PMP_REGION_OVERLAP;
    }
  }

  /* PMP granularity check */
  if(size != -1UL && (size & (RISCV_PGSIZE - 1)))
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_NOT_PAGE_GRANULARITY, "PMP granularity is RISCV_PGSIZE");
  if(start & (RISCV_PGSIZE - 1))
    PMP_ERROR(SBI_ERR_SM_PMP_REGION_NOT_PAGE_GRANULARITY, "PMP granularity is RISCV_PGSIZE");

  /* if the address covers the entire RAM or it's NAPOT */
  if ((size == -1UL && start == 0) ||
      (!(size & (size - 1)) && !(start & (size - 1)))) {
    return napot_region_init(start, size, priority, rid, allow_overlap);
  }
  else
  {
    if(priority != PMP_PRI_ANY &&
      (priority != PMP_PRI_TOP || start != 0)) {
      PMP_ERROR(SBI_ERR_SM_PMP_REGION_IMPOSSIBLE_TOR, "The top-priority TOR PMP entry must start from address 0");
    }

    return tor_region_init(start, size, priority, rid, allow_overlap);
  }
}

uintptr_t pmp_region_get_addr(region_id i)
{
  if(is_pmp_region_valid(i))
    return region_get_addr(i);
  return 0;
}

uint64_t pmp_region_get_size(region_id i)
{
  if(is_pmp_region_valid(i))
    return region_get_size(i);
  return 0;
}
