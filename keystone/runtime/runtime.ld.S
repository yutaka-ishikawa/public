#include "mm/vm_defs.h"

OUTPUT_ARCH( "riscv" )
ENTRY(_start)

SECTIONS
{
  . = 0xffffffffc0000000;
  PROVIDE(rt_base = .);
  .text : {
    KEEP(*(.text._start))
    *(.text.encl_trap_handler)
    *(.text)
  }
  . = ALIGN(RISCV_PAGE_SIZE);
  .rodata :
  {
    *(.rdata)
    *(.rodata)
  }
  .data : { *(.data) }
  .bss : { *(.bss) }
  . = ALIGN(RISCV_PAGE_SIZE);
  .kernel_stack : {
    . += 8 * RISCV_PAGE_SIZE;
    PROVIDE(kernel_stack_end = .);
  }

  _end = .;

  /* The loader enters RUNTIME_VA_START, not the ELF entry field. Some
   * toolchains place orphan build-id notes before .text at this address. */
  /DISCARD/ : { *(.note.gnu.build-id) }
  ASSERT(_start == rt_base, "Eyrie entry must equal RUNTIME_VA_START")
}
