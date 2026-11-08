//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include "riscv64.h"
#include <linux/kernel.h>
#include "keystone.h"
#include <linux/dma-mapping.h>
#include <linux/version.h>

/* Destroy all memory associated with an EPM. */
int epm_destroy(struct epm *epm)
{
    if (!epm->ptr || !epm->size) {
	return 0;
    }
    if (epm->is_dma) {
	dma_free_pages(keystone_dev.this_device, epm->size,
		       virt_to_page((void *)epm->ptr), epm->dma_handle,
		       DMA_BIDIRECTIONAL);
    } else {
	free_pages(epm->ptr, epm->order);
    }
    memset(epm, 0, sizeof *epm);
    return 0;
}

/* EPM needs contiguous CPU physical pages, not a coherent DMA mapping. */
int epm_init(struct epm *epm, unsigned int min_pages)
{
    vaddr_t epm_vaddr;
    unsigned long order;
    unsigned long count;
    struct page *pages;
    dma_addr_t dma_handle;

    /* A failed initialization must also be safe for destroy_enclave(). */
    memset(epm, 0, sizeof *epm);
    if (!min_pages) {
	return -EINVAL;
    }
    order = get_order((size_t)min_pages << PAGE_SHIFT);
    count = 1UL << order;
    epm_vaddr = 0;

    /* MAX_PAGE_ORDER is inclusive; the old MAX_ORDER was exclusive. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
    if (order <= MAX_PAGE_ORDER) {
#else
    if (order < MAX_ORDER) {
#endif
	epm_vaddr = (vaddr_t)__get_free_pages(GFP_KERNEL, order);
    }
    if (!epm_vaddr) {
	count = min_pages;
	/* On K3 DMA-direct uses CMA for requests beyond the buddy limit.
	 * Keep the DMA handle for release, but give the SM the CPU PA.
	 */
	pages = dma_alloc_pages(keystone_dev.this_device, count << PAGE_SHIFT,
				&dma_handle, DMA_BIDIRECTIONAL,
				GFP_KERNEL | __GFP_NOWARN);
	if (pages) {
	    epm_vaddr = (vaddr_t)page_address(pages);
	    if (!epm_vaddr) {
		dma_free_pages(keystone_dev.this_device, count << PAGE_SHIFT,
			       pages, dma_handle, DMA_BIDIRECTIONAL);
	    } else {
		epm->is_dma = true;
		epm->dma_handle = dma_handle;
		epm->pa = page_to_phys(pages);
	    }
	}
    }
    if (!epm_vaddr) {
	keystone_err("failed to allocate %lu contiguous EPM page(s)\n", count);
	return -ENOMEM;
    }
    memset((void *)epm_vaddr, 0, PAGE_SIZE * count);
    epm->root_page_table = (void *)epm_vaddr;
    if (!epm->is_dma) {
	epm->pa = __pa(epm_vaddr);
    }
    epm->order = order;
    epm->size = count << PAGE_SHIFT;
    epm->ptr = epm_vaddr;
    keystone_info("EPM allocated: %zu bytes at %lx (%s)\n", epm->size,
		  (unsigned long)epm->pa, epm->is_dma ? "DMA/CMA" : "buddy");
    return 0;
}

int utm_destroy(struct utm* utm){

  if(utm->ptr != NULL){
    free_pages((vaddr_t)utm->ptr, utm->order);
  }

  return 0;
}

int utm_init(struct utm* utm, size_t untrusted_size)
{
  unsigned long req_pages = 0;
  unsigned long order = 0;
  unsigned long count;
  req_pages += PAGE_UP(untrusted_size)/PAGE_SIZE;
  order = ilog2(req_pages - 1) + 1;
  count = 0x1 << order;

  utm->order = order;

  /* Currently, UTM does not utilize CMA.
   * It is always allocated from the buddy allocator */
  utm->ptr = (void*) __get_free_pages(GFP_HIGHUSER, order);
  if (!utm->ptr) {
    keystone_err("failed to allocate UTM (size = %i bytes)\n",(1<<order));
    return -ENOMEM;
  }

  utm->size = count * PAGE_SIZE;
  if (utm->size != untrusted_size) {
    /* Instead of failing, we just warn that the user has to fix the parameter. */
    keystone_warn("shared buffer size is not multiple of PAGE_SIZE\n");
  }

  return 0;
}
