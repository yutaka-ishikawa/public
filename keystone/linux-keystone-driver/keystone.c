//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
//#include <asm/io.h>
//#include <asm/page.h>
#include "keystone.h"
#include "keystone-sbi.h"

#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include "keystone_user.h"
#define   DRV_DESCRIPTION   "keystone enclave"
#define   DRV_VERSION       "1.0.0"

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR("Dayeol Lee <dayeol@berkeley.edu>");
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("Dual BSD/GPL");

atomic_long_t keystone_backing_bytes = ATOMIC_LONG_INIT(0);

static int backing_bytes_get(char *buffer, const struct kernel_param *param)
{
    return scnprintf(buffer, PAGE_SIZE, "%ld\n",
		     atomic_long_read(&keystone_backing_bytes));
}

static const struct kernel_param_ops backing_bytes_ops = {
    .get = backing_bytes_get,
};
module_param_cb(backing_bytes, &backing_bytes_ops, NULL, 0444);
MODULE_PARM_DESC(backing_bytes, "Bytes of currently owned multimem backing");

static int keystone_open(struct inode *inode, struct file *file)
{
    struct keystone_file *ctx;

    ctx = kzalloc(sizeof *ctx, GFP_KERNEL);
    if (!ctx) {
	return -ENOMEM;
    }
    mutex_init(&ctx->lock);
    file->private_data = ctx;
    return 0;
}

static const struct file_operations keystone_fops = {
    .open = keystone_open,
    .owner = THIS_MODULE,
    .mmap = keystone_mmap,
    .unlocked_ioctl = keystone_ioctl,
    .release = keystone_release};

struct miscdevice keystone_dev = {
  .minor = MISC_DYNAMIC_MINOR,
  .name = "keystone_enclave",
  .fops = &keystone_fops,
  .mode = 0666,
};

static int keystone_mmap_locked(struct file *filp, struct vm_area_struct *vma)
{
    struct utm *utm;
    struct epm *epm;
    struct enclave *enclave;
    unsigned long vsize, psize;
    vaddr_t paddr;
    enclave =
	get_enclave_by_id(((struct keystone_file *)filp->private_data)->ueid);
    if (!enclave) {
	keystone_err("invalid enclave id\n");
	return -EINVAL;
    }

    utm = enclave->utm;
    epm = enclave->epm;
    vsize = vma->vm_end - vma->vm_start;

    if (enclave->is_init) {
	if (vsize > PAGE_SIZE) {
	    return -EINVAL;
	}
	paddr = epm->pa + (vma->vm_pgoff << PAGE_SHIFT);
	remap_pfn_range(vma, vma->vm_start, paddr >> PAGE_SHIFT, vsize,
			vma->vm_page_prot);
    } else {
	psize = utm->size;
	if (vsize > psize) {
	    return -EINVAL;
	}
	remap_pfn_range(vma, vma->vm_start, __pa(utm->ptr) >> PAGE_SHIFT, vsize,
			vma->vm_page_prot);
    }
    return 0;
}

int keystone_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct keystone_file *ctx;
    int rc;

    ctx = file->private_data;
    mutex_lock(&ctx->lock);
    rc = keystone_mmap_locked(file, vma);
    mutex_unlock(&ctx->lock);
    return rc;
}

static int __init keystone_dev_init(void)
{
    int ret;

    ret = misc_register(&keystone_dev);
    if (ret < 0) {
	pr_err("keystone_enclave: misc_register() failed\n");
	return ret;
    }
    /* K3 RAM starts above 4 GiB. EPM is not a 32-bit DMA device. */
    ret = dma_coerce_mask_and_coherent(keystone_dev.this_device,
				       DMA_BIT_MASK(64));
    if (ret) {
	misc_deregister(&keystone_dev);
	return ret;
    }
    pr_info("keystone_enclave: " DRV_DESCRIPTION " v" DRV_VERSION "\n");
    return 0;
}

static void __exit keystone_dev_exit(void)
{
  pr_info("keystone_enclave: keystone_dev_exit()\n");
  misc_deregister(&keystone_dev);
  return;
}

module_init(keystone_dev_init);
module_exit(keystone_dev_exit);
