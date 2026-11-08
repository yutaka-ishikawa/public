/* SPDX-License-Identifier: BSD-2-Clause */
/* Native Eyrie entry: the runtime supplies sp, this stub supplies the C ABI gp.
 * It is pulled from the archive only by an explicit native entry selection. */
.section .text.keystone_eapp_start, "ax", @progbits
.globl keystone_eapp_start
.type keystone_eapp_start, @function
.balign 4
keystone_eapp_start:
    .option push
    .option norelax
    .option norvc
    lla gp, __global_pointer$
    .option pop
    call main
    call EAPP_RETURN
1:
    j 1b
.size keystone_eapp_start, .-keystone_eapp_start
