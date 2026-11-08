#include "mm/common.h"
#include "mm/mm.h"
#include "mm/vm.h"
#include "mm/freemem.h"
#include "mm/paging.h"

/* Page table utilities */
static pte*
__walk_create(pte* root, uintptr_t addr);

/* Hacky storage of current u-mode break */
static uintptr_t current_program_break;

uintptr_t get_program_break()
{
  return current_program_break;
}

void set_program_break(uintptr_t new_break)
{
  current_program_break = new_break;
}

static pte*
__continue_walk_create(pte* root, uintptr_t addr, pte* pte)
{
  uintptr_t new_page = spa_get_zero();
  assert(new_page);

  unsigned long free_ppn = ppn(__pa(new_page));
  *pte = ptd_create(free_ppn);
  return __walk_create(root, addr);
}

static pte*
__walk_internal(pte* root, uintptr_t addr, int create)
{
  pte* t = root;
  int i;
  for (i = 1; i < RISCV_PT_LEVELS; i++)
  {
    size_t idx = RISCV_GET_PT_INDEX(addr, i);

    if (!(t[idx] & PTE_V))
      return create ? __continue_walk_create(root, addr, &t[idx]) : 0;

    t = (pte*) __va(pte_ppn(t[idx]) << RISCV_PAGE_BITS);
  }

  return &t[RISCV_GET_PT_INDEX(addr, RISCV_PT_LEVELS)];
}

/* walk the page table and return PTE
 * return 0 if no mapping exists */
static pte*
__walk(pte* root, uintptr_t addr)
{
  return __walk_internal(root, addr, 0);
}

/* walk the page table and return PTE
 * create the mapping if non exists */
static pte*
__walk_create(pte* root, uintptr_t addr)
{
  return __walk_internal(root, addr, 1);
}

/* Create a virtual memory mapping between a physical and virtual page */
uintptr_t 
map_page(uintptr_t vpn, uintptr_t ppn, int flags)
{
  pte* pte = __walk_create(root_page_table, vpn << RISCV_PAGE_BITS);

  // TODO: what is supposed to happen if page is already allocated?
  if (*pte & PTE_V) {
    return -1;
  }

  *pte = pte_create(ppn, PTE_D | PTE_A | PTE_V | flags);
  return 1;
}

/* allocate a new page to a given vpn
 * returns VA of the page, (returns 0 if fails) */
uintptr_t
alloc_page(uintptr_t vpn, int flags)
{
  uintptr_t page;
  pte* pte = __walk_create(root_page_table, vpn << RISCV_PAGE_BITS);

  if (!pte)
    return 0;

	/* if the page has been already allocated, return the page */
  if(*pte & PTE_V) {
    return __va(*pte << RISCV_PAGE_BITS);
  }

	/* otherwise, allocate one from the freemem */
  page = spa_get_zero();
  assert(page);

  *pte = pte_create(ppn(__pa(page)), PTE_D | PTE_A | PTE_V | flags);
#ifdef USE_PAGING
  paging_inc_user_page();
#endif

  return page;
}

uintptr_t
realloc_page(uintptr_t vpn, int flags)
{
  assert(flags & PTE_U);

  pte *pte = __walk(root_page_table, vpn << RISCV_PAGE_BITS);
  if(!pte)
    return 0;

  if(*pte & PTE_V) {
    *pte = pte_create(pte_ppn(*pte), flags);
    return __va(*pte << RISCV_PAGE_BITS);
  }

  return 0;
}

void
free_page(uintptr_t vpn)
{

  pte* pte = __walk(root_page_table, vpn << RISCV_PAGE_BITS);

  // No such PTE, or invalid
  if(!pte || !(*pte & PTE_V))
    return;

  assert(*pte & PTE_U);

  uintptr_t ppn = pte_ppn(*pte);
  // Mark invalid
  // TODO maybe do more here
  *pte = 0;

#ifdef USE_PAGING
  paging_dec_user_page();
#endif
  // Return phys page
  spa_put(__va(ppn << RISCV_PAGE_BITS));

  return;

}

/* allocate n new pages from a given vpn
 * returns the number of pages allocated */
size_t
alloc_pages(uintptr_t vpn, size_t count, int flags)
{
  unsigned int i;
  for (i = 0; i < count; i++) {
    if(!alloc_page(vpn + i, flags))
      break;
  }

  return i;
}

void
free_pages(uintptr_t vpn, size_t count){
  unsigned int i;
  for (i = 0; i < count; i++) {
    free_page(vpn + i);
  }

}

/*
 * Check if a range of VAs contains any allocated pages, starting with
 * the given VA. Returns the number of sequential pages that meet the
 * conditions.
 */
size_t
test_va_range(uintptr_t vpn, size_t count){

  unsigned int i;
  /* Validate the region */
  for (i = 0; i < count; i++) {
    pte* pte = __walk_internal(root_page_table, (vpn+i) << RISCV_PAGE_BITS, 0);
    // If the page exists and is valid then we cannot use it
    if(pte && *pte){
      break;
    }
  }
  return i;
}

/* get a mapped physical address for a VA */
uintptr_t
translate(uintptr_t va)
{
  pte* pte = __walk(root_page_table, va);

  if(pte && (*pte & PTE_V))
    return (pte_ppn(*pte) << RISCV_PAGE_BITS) | (RISCV_PAGE_OFFSET(va));
  else
    return 0;
}

/* try to retrieve PTE for a VA, return 0 if fail */
pte*
pte_of_va(uintptr_t va)
{
  pte* pte = __walk(root_page_table, va);
  return pte;
}


void
__map_with_reserved_page_table_32(uintptr_t dram_base,
                               uintptr_t dram_size,
                               uintptr_t ptr,
                               pte* l2_pt)
{
  uintptr_t offset = 0;
  uintptr_t leaf_level = 2;
  pte* leaf_pt = l2_pt;
  unsigned long dram_max =  RISCV_GET_LVL_PGSIZE(leaf_level - 1);

  /* use megapage if l2_pt is null */
  if (!l2_pt) {
    leaf_level = 1;
    leaf_pt = root_page_table;
    dram_max = -1UL; 
  }

  assert(dram_size <= dram_max);
  assert(IS_ALIGNED(dram_base, RISCV_GET_LVL_PGSIZE_BITS(leaf_level)));
  assert(IS_ALIGNED(ptr, RISCV_GET_LVL_PGSIZE_BITS(leaf_level - 1)));

  if(l2_pt) {
       /* set root page table entry */
       root_page_table[RISCV_GET_PT_INDEX(ptr, 1)] =
       ptd_create(ppn(kernel_va_to_pa(l2_pt)));
  }

  for (offset = 0;
       offset < dram_size;
       offset += RISCV_GET_LVL_PGSIZE(leaf_level))
  {
        leaf_pt[RISCV_GET_PT_INDEX(ptr + offset, leaf_level)] =
        pte_create(ppn(dram_base + offset),
                 PTE_R | PTE_W | PTE_X | PTE_A | PTE_D);
  }

}

/* Map one Sv39 L1 window. CMA only guarantees page alignment, so a
 * physical range need not be aligned for 2 MiB leaves. Use those leaves
 * only where both addresses and the remaining size permit them.
 */
void __map_with_reserved_page_table_64(uintptr_t dram_base, uintptr_t dram_size,
				       uintptr_t ptr, pte *l2_pt, pte *l3_pt)
{
    uintptr_t offset;
    uintptr_t physical;
    uintptr_t virtual_addr;
    uintptr_t table_pa;
    uintptr_t page;
    size_t i;
    pte *table;
    bool reserved_used;
    int permissions;

    assert(l2_pt && l3_pt);
    assert(dram_size && dram_size <= RISCV_GET_LVL_PGSIZE(1));
    assert(dram_base + dram_size >= dram_base);
    assert(IS_ALIGNED(dram_base, RISCV_PAGE_BITS));
    assert(IS_ALIGNED(dram_size, RISCV_PAGE_BITS));
    assert(IS_ALIGNED(ptr, RISCV_GET_LVL_PGSIZE_BITS(1)));

    root_page_table[RISCV_GET_PT_INDEX(ptr, 1)] =
	ptd_create(ppn(kernel_va_to_pa(l2_pt)));
    permissions = PTE_R | PTE_W | PTE_X | PTE_A | PTE_D;
    offset = 0;
    reserved_used = false;
    while (offset < dram_size) {
	physical = dram_base + offset;
	virtual_addr = ptr + offset;
	if (IS_ALIGNED(physical, RISCV_GET_LVL_PGSIZE_BITS(2)) &&
	    IS_ALIGNED(virtual_addr, RISCV_GET_LVL_PGSIZE_BITS(2)) &&
	    dram_size - offset >= RISCV_GET_LVL_PGSIZE(2)) {
	    l2_pt[RISCV_GET_PT_INDEX(virtual_addr, 2)] =
		pte_create(ppn(physical), permissions);
	    offset += RISCV_GET_LVL_PGSIZE(2);
	    continue;
	}
	if (!reserved_used) {
	    table = l3_pt;
	    table_pa = kernel_va_to_pa(table);
	    reserved_used = true;
	} else {
	    page = spa_get_zero();
	    assert(page);
	    table = (pte *)page;
	    /* Allocated pages use the load/direct map, whereas static
	     * reserved tables belong to the runtime's kernel mapping.
	     * Both conversions are identity mappings in the raw loader.
	     */
	    table_pa = __pa(page);
	}
	l2_pt[RISCV_GET_PT_INDEX(virtual_addr, 2)] = ptd_create(ppn(table_pa));
	for (i = 0; i < BIT(RISCV_PT_INDEX_BITS) && offset < dram_size; i++) {
	    table[RISCV_GET_PT_INDEX(ptr + offset, 3)] =
		pte_create(ppn(dram_base + offset), permissions);
	    offset += RISCV_PAGE_SIZE;
	}
    }
}

void map_with_reserved_page_table(uintptr_t dram_base, uintptr_t dram_size,
				  uintptr_t ptr, pte *l2_pt, pte *l3_pt)
{
#if __riscv_xlen == 64
    __map_with_reserved_page_table_64(dram_base, dram_size, ptr, l2_pt, l3_pt);
#elif __riscv_xlen == 32
    if (dram_size > RISCV_GET_LVL_PGSIZE(1)) {
	__map_with_reserved_page_table_32(dram_base, dram_size, ptr, 0);
    } else {
	__map_with_reserved_page_table_32(dram_base, dram_size, ptr, l2_pt);
    }
#endif
}
