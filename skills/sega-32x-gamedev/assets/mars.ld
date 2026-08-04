/*
 * mars.ld - SH-2 linker script for a Sega 32X ROM.
 *
 * Memory map:
 *   0x02000000  ROM   .text/.rodata (cartridge, read-only)
 *   0x06000000  SDRAM .data (copied from ROM by startup), then .bss, then heap
 *   0x0603FC00  top of master SH-2 stack (grows down)
 *
 * If you use the SLAVE SH-2, give it its own stack: lower ram LENGTH to
 * 0x0003F800, set the master __stack to 0x0603F800, and reserve
 * 0x0603F800..0x06040000 for the slave stack in your startup code.
 */

OUTPUT_ARCH(sh)
EXTERN (_start)
ENTRY (_start)
__DYNAMIC = 0;

MEMORY
{
    rom (rx) : ORIGIN = 0x02000000, LENGTH = 0x00400000   /* up to ~4 MiB   */
    ram (wx) : ORIGIN = 0x06000000, LENGTH = 0x0003FC00    /* 256 KiB SDRAM  */
}

PROVIDE (__stack = 0x0603FC00);

SECTIONS
{
  .text 0x02000000 : AT ( 0x00000000 )
  {
    __text_start = .;
    *(.text) *(.text.*) *(.gnu.linkonce.t.*)

    . = ALIGN(16);
    __INIT_SECTION__ = .;
    KEEP (*(.init))
    SHORT (0x000B) SHORT (0x0009)   /* rts ; nop */
    . = ALIGN(16);
    __FINI_SECTION__ = .;
    KEEP (*(.fini))
    SHORT (0x000B) SHORT (0x0009)   /* rts ; nop */

    *(.eh_frame_hdr) KEEP (*(.eh_frame))
    *(.gcc_except_table) *(.gcc_except_table.*)
    KEEP (*(.jcr))

    . = ALIGN(16);
    __CTOR_LIST__ = .; ___CTOR_LIST__ = .;
    LONG((__CTOR_END__ - __CTOR_LIST__) / 4 - 2)
    KEEP (*(SORT(.ctors.*))) KEEP (*(.ctors))
    LONG(0) __CTOR_END__ = .;

    . = ALIGN(16);
    __DTOR_LIST__ = .; ___DTOR_LIST__ = .;
    LONG((__DTOR_END__ - __DTOR_LIST__) / 4 - 2)
    KEEP (*(SORT(.dtors.*))) KEEP (*(.dtors))
    LONG(0) __DTOR_END__ = .;

    *(.rdata) *(.rodata) *(.rodata.*) *(.gnu.linkonce.r.*)
    . = ALIGN(16);
    __text_end = .;
  } > rom
  __text_size = __text_end - __text_start;

  .data 0x06000000 : AT ( LOADADDR (.text) + SIZEOF (.text) )
  {
    __data_start = .;
    *(.data) *(.data.*) *(.gnu.linkonce.d.*)
    CONSTRUCTORS
    *(.lit8) *(.lit4) *(.sdata) *(.sdata.*) *(.gnu.linkonce.s.*)
    . = ALIGN(16);
    __data_end = .;
  } > ram
  __data_size = __data_end - __data_start;

  .bss :
  {
    __bss_start = .;
    *(.bss) *(.bss.*) *(.gnu.linkonce.b.*)
    *(.sbss) *(.sbss.*) *(.gnu.linkonce.sb.*)
    *(.scommon) *(COMMON)
    . = ALIGN(16);
    end = .; _end = end; __end = _end;
    __bss_end = .;
  } > ram
  __bss_size = __bss_end - __bss_start;
}
