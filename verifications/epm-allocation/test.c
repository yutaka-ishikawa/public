#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifndef TEST_VERSION
#define TEST_VERSION 608
#endif
#define KERNEL_VERSION(a, b, c) ((a) * 100 + (b))
#define LINUX_VERSION_CODE TEST_VERSION
#define MAX_PAGE_ORDER 10
#define MAX_ORDER 11
#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define GFP_KERNEL 1
#define __GFP_NOWARN 2
#define DMA_BIDIRECTIONAL 3
#define DMA_BIT_MASK(bits) (UINT64_MAX >> (64 - (bits)))
#define __init
#define DRV_DESCRIPTION "test"
#define DRV_VERSION "test"
#define pr_info(...) ((void)0)
#define pr_err(...) ((void)0)
#define keystone_info(...) ((void)0)
#define keystone_err(...) ((void)0)
#define keystone_warn(...) ((void)0)
typedef uintptr_t vaddr_t;
typedef uint64_t dma_addr_t;

typedef uintptr_t pte_t;
typedef uintptr_t paddr_t;
#include "build/types.inc"

struct page {
    void *address;
};

struct device {
    int unused;
};

struct miscdevice {
    struct device *this_device;
};
static struct device device;
static struct miscdevice keystone_dev = {&device};
static struct page dma_page;
static bool fail_buddy, fail_dma;
static unsigned int buddy_calls, dma_calls, frees, deregisters;
static int register_result, mask_result;
static uint64_t mask;
static size_t allocation_size;

static unsigned long get_order(size_t size)
{
    unsigned long order;

    order = 0;
    while ((PAGE_SIZE << order) < size) {
	order++;
    }
    return order;
}

static uintptr_t __pa(uintptr_t address)
{
    (void)address;
    return 0x110000000UL;
}

static void *allocate(size_t size)
{
    void *ptr;

    ptr = malloc(size);
    assert(ptr);
    memset(ptr, 0x5a, size);
    allocation_size = size;
    return ptr;
}

static uintptr_t __get_free_pages(int flags, unsigned long order)
{
    assert(flags == GFP_KERNEL);
    assert(order <= MAX_PAGE_ORDER);
    buddy_calls++;
    if (fail_buddy) {
	return 0;
    }
    return (uintptr_t)allocate(PAGE_SIZE << order);
}

static struct page *dma_alloc_pages(struct device *dev, size_t size,
				    dma_addr_t *handle, int direction,
				    int flags)
{
    assert(dev == &device && direction == DMA_BIDIRECTIONAL);
    assert(flags == (GFP_KERNEL | __GFP_NOWARN));
    dma_calls++;
    if (fail_dma) {
	return NULL;
    }
    dma_page.address = allocate(size);
    *handle = 0x220000000UL;
    return &dma_page;
}

static void *page_address(struct page *page)
{
    return page->address;
}

static uintptr_t page_to_phys(struct page *page)
{
    assert(page == &dma_page);
    return 0x130000000UL;
}

static struct page *virt_to_page(void *address)
{
    assert(address == dma_page.address);
    return &dma_page;
}

static void free_pages(uintptr_t ptr, unsigned long order)
{
    assert(allocation_size == (PAGE_SIZE << order));
    frees++;
    free((void *)ptr);
}

static void dma_free_pages(struct device *dev, size_t size, struct page *page,
			   dma_addr_t handle, int direction)
{
    assert(dev == &device && size == allocation_size);
    assert(page == &dma_page && handle == 0x220000000UL);
    assert(direction == DMA_BIDIRECTIONAL);
    frees++;
    free(page->address);
    page->address = NULL;
}

static int misc_register(struct miscdevice *dev)
{
    assert(dev == &keystone_dev);
    return register_result;
}

static void misc_deregister(struct miscdevice *dev)
{
    assert(dev == &keystone_dev);
    deregisters++;
}

static int dma_coerce_mask_and_coherent(struct device *dev, uint64_t value)
{
    assert(dev == &device);
    mask = value;
    return mask_result;
}

struct enclave {
    unsigned long eid;
};

struct sbiret {
    long error;
};
static struct enclave test_enclave;
static unsigned int sm_destroys, enclave_frees, id_removes;
static long sm_error;

static struct enclave *get_enclave_by_id(unsigned int id)
{
    assert(id == 1);
    return &test_enclave;
}

static struct sbiret sbi_sm_destroy_enclave(unsigned long id)
{
    struct sbiret result;

    assert(id == 7);
    sm_destroys++;
    result.error = sm_error;
    return result;
}

static int destroy_enclave(struct enclave *enclave)
{
    assert(enclave == &test_enclave);
    enclave_frees++;
    return 0;
}

static void enclave_idr_remove(unsigned int id)
{
    assert(id == 1);
    id_removes++;
}

#include "build/production.inc"

static void reset(void)
{
    fail_buddy = false;
    fail_dma = false;
    buddy_calls = dma_calls = frees = deregisters = 0;
    register_result = mask_result = 0;
    mask = 0;
}

static void check_allocation(unsigned int pages, bool dma, size_t size)
{
    struct epm epm;
    unsigned char *bytes;
    size_t i;

    assert(epm_init(&epm, pages) == 0);
    assert(epm.size == size && epm.is_dma == dma);
    assert(epm.pa == (dma ? 0x130000000UL : 0x110000000UL));
    bytes = (void *)epm.ptr;
    for (i = 0; i < epm.size; i++) {
	assert(bytes[i] == 0);
    }
    bytes[0] = 1;
    bytes[epm.size - 1] = 2;
    assert(epm_destroy(&epm) == 0 && frees == 1);
    assert(!epm.ptr && !epm.size);
    assert(epm_destroy(&epm) == 0 && frees == 1);
}

int main(void)
{
    struct epm epm;

    reset();
    check_allocation(566, false, 4UL << 20);
    assert(buddy_calls == 1 && dma_calls == 0);
    puts("PASS EPM 01: 566 pages use valid order-10 buddy allocation");

    reset();
    check_allocation(1024, false, 4UL << 20);
    puts("PASS EPM 02: exactly 4 MiB boundary");

    reset();
    fail_buddy = true;
    check_allocation(566, true, 566 * PAGE_SIZE);
    assert(buddy_calls == 1 && dma_calls == 1);
    puts("PASS EPM 03: fallback retains CPU PA and distinct DMA handle");

    reset();
    check_allocation(65536, true, 256UL << 20);
    assert(buddy_calls == 0 && dma_calls == 1);
    puts("PASS EPM 04: 256 MiB bypasses buddy; zeroed and released");

    reset();
    check_allocation(65590, true, 65590 * PAGE_SIZE);
    assert(buddy_calls == 0 && dma_calls == 1);
    puts("PASS EPM 05: 256 MiB plus ELF overhead keeps exact page count");

    reset();
    fail_buddy = fail_dma = true;
    memset(&epm, 0xa5, sizeof epm);
    assert(epm_init(&epm, 566) == -ENOMEM);
    assert(epm_destroy(&epm) == 0 && frees == 0);
    puts("PASS EPM 06: allocation failure leaves safe cleanup state");

    reset();
    assert(epm_init(&epm, 0) == -EINVAL);
    assert(!buddy_calls && !dma_calls && epm_destroy(&epm) == 0);
    puts("PASS EPM 07: zero-page request rejected before allocation");

    reset();
    assert(keystone_dev_init() == 0 && mask == UINT64_MAX);
    assert(!deregisters);
    register_result = -EBUSY;
    mask = 0;
    assert(keystone_dev_init() == -EBUSY && !mask);
    register_result = 0;
    mask_result = -EIO;
    assert(keystone_dev_init() == -EIO && deregisters == 1);
    puts("PASS EPM 08: 64-bit mask and registration error cleanup");
    test_enclave.eid = (unsigned long)-1;
    assert(__keystone_destroy_enclave(1) == 0);
    assert(!sm_destroys && enclave_frees == 1 && id_removes == 1);
    test_enclave.eid = 7;
    sm_error = -1;
    assert(__keystone_destroy_enclave(1) == -EINVAL);
    assert(sm_destroys == 1 && enclave_frees == 1 && id_removes == 1);
    sm_error = 0;
    assert(__keystone_destroy_enclave(1) == 0);
    assert(sm_destroys == 2 && enclave_frees == 2 && id_removes == 2);
    puts("PASS EPM 09: pre-finalize cleanup skips SM; SM failures retain EPM");
    printf("SUMMARY EPM: 9 groups passed (kernel %d; allocators mocked)\n",
	   TEST_VERSION);
    return 0;
}
