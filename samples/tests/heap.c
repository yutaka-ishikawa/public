/* SDK tiny-malloc uses the address of this symbol as its heap start.
 * These small examples reserve a bounded arena in their own ELF .bss. */
unsigned char __malloc_start[65536] __attribute__((aligned(16)));
