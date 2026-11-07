OUTPUT_ARCH("riscv")

ENTRY( _start )

PAGE_SIZE = 0x1000; /* TODO: figure out the page size to use here */ 
STACK_SIZE = DEFINED(__stack_size__) ? __stack_size__ : 0x1000;

SECTIONS
{
  .text :
  {
    PROVIDE( _start = . );
    *(._start)
    *(.text .text.*)
    *(.rdata .rdata.*)
    *(.rodata .rodata.* .srodata .srodata.*)
    *(.data .data.* .sdata .sdata.*)
    *(.bss .bss.* .sbss .sbss.*)
    *(COMMON)

    /* 
    * Only text section is copied into final loader binary 
    * Stack is added to text section as temporary stack space for loader
    */
    . = ALIGN(16);
    PROVIDE ( _sstack = . );
    *(._sstack);
    . = . + STACK_SIZE;
    . = ALIGN(16);
    PROVIDE ( _estack = . );
    *(._estack);
  }

  /* A raw image has no mechanism to relocate absolute GOT addresses. */
  .got : { *(.got .got.* .igot .igot.*) }
  ASSERT(SIZEOF(.got) == 0, "raw Keystone loader contains an unsupported GOT")
}
