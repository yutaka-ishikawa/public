// SPDX-License-Identifier: GPL-2.0-only
/* Read a valid RAM direct mapping in S-mode; no access is made through
 * /dev/mem. */
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include "probe_abi.h"

static int probe_ram_byte(u64 physical, u8 *value)
{
    void *address;

    if ((u64)(phys_addr_t)physical != physical ||
	!pfn_valid(PHYS_PFN(physical))) {
	return -EINVAL;
    }
    address = phys_to_virt((phys_addr_t)physical);
    if (!virt_addr_valid(address)) {
	return -EINVAL;
    }
    return copy_from_kernel_nofault(value, address, 1);
}

static long probe_ioctl(struct file *file, unsigned int command,
			unsigned long argument)
{
    struct edge_verify_probe request;
    u8 *control;

    if (command != EDGE_VERIFY_PROBE) {
	return -ENOTTY;
    }
    if (!capable(CAP_SYS_RAWIO)) {
	return -EPERM;
    }
    if (copy_from_user(&request, (void __user *)argument, sizeof(request))) {
	return -EFAULT;
    }
    if (request.private_pa == request.shared_pa) {
	return -EINVAL;
    }
    control = kmalloc(1, GFP_KERNEL);
    if (!control) {
	return -ENOMEM;
    }
    *control = 0x6d;
    request.private_byte = 0;
    request.shared_byte = 0;
    request.control_byte = 0;
    request.reserved = 0;
    request.control_result =
	copy_from_kernel_nofault(&request.control_byte, control, 1);
    request.shared_result =
	probe_ram_byte(request.shared_pa, &request.shared_byte);
    request.private_result =
	probe_ram_byte(request.private_pa, &request.private_byte);
    kfree(control);
    if (copy_to_user((void __user *)argument, &request, sizeof(request))) {
	return -EFAULT;
    }
    return 0;
}

static const struct file_operations probe_operations = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = probe_ioctl,
};

static struct miscdevice probe_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "keystone_edge_probe",
    .fops = &probe_operations,
    .mode = 0600,
};

static int __init probe_init(void)
{
    return misc_register(&probe_device);
}

static void __exit probe_exit(void)
{
    misc_deregister(&probe_device);
}

module_init(probe_init);
module_exit(probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Keystone edgecall EPM read-denial verification");
