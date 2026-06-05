OUTPUT_ARCH("loongarch")
ENTRY(_entry)

SECTIONS
{
  . = 0x200000;

  .text : {
    *(.text.entry)
    *(.text .text.*)
  }

  .rodata : {
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  .data : {
    . = ALIGN(16);
    *(.data .data.*)
  }

  .bss : {
    . = ALIGN(16);
    *(.bss .bss.* COMMON)
    . = ALIGN(16);
    la_boot_stack = .;
    . += 4096;
    la_boot_stack_top = .;
  }
}
