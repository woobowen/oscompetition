OUTPUT_ARCH("loongarch")
ENTRY(_entry)

SECTIONS
{
  . = 0x200000;

  .text : {
    *(.text.entry)
    *(.text .text.*)
  }

  . = ALIGN(0x1000);
  .text.trap : {
    *(.text.trap)
  }

  . = ALIGN(0x1000);
  .text.userret : {
    *(.text.userret)
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
    . += 16384;
    la_boot_stack_top = .;
    . = ALIGN(0x1000);
    la_kernel_end = .;
  }
}
