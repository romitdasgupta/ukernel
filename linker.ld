/*
 * Helios64 AArch64 Microkernel Linker Script
 * Targets QEMU 'virt' machine (RAM starts at 0x40000000, kernel loaded at 0x40080000)
 */
ENTRY(_start)

SECTIONS
{
    . = 0x40080000;

    __kernel_start = .;

    /* Code and Read-Only Data (4KB Page Aligned) */
    . = ALIGN(4096);
    __text_start = .;
    .text : {
        KEEP(*(.text.boot))
        . = ALIGN(2048);
        KEEP(*(.text.vectors))
        *(.text .text.*)
    }

    . = ALIGN(4096);
    __text_end = .;
    __rodata_start = .;
    .rodata : {
        *(.rodata .rodata.*)
    }
    . = ALIGN(4096);
    __rodata_end = .;

    /* Privileged Kernel Data & BSS (Strictly EL1-Only in MMU) */
    __data_start = .;
    .data : {
        *(.data .data.*)
    }
    . = ALIGN(4096);
    __data_end = .;

    __bss_start = .;
    .bss (NOLOAD) : {
        *(.bss .bss.*)
        *(COMMON)
    }
    . = ALIGN(4096);
    __bss_end = .;

    /* Initial Boot / Idle Stack (16 KB) */
    . = ALIGN(4096);
    __boot_stack_bottom = .;
    . += 0x4000;
    __boot_stack_top = .;

    . = ALIGN(4096);
    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
