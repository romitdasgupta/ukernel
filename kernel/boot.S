/*
 * Helios64 Microkernel - AArch64 Boot & Context Switch Assembly
 */

.section ".text.boot"
.global _start

_start:
    /* Read MPIDR_EL1 and ensure only primary core (CPU 0) proceeds */
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    cbnz    x0, .Lpark_cpu

    /* Check Current Exception Level (CurrentEL bits [3:2]) */
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #3
    b.eq    .Lfrom_el3
    cmp     x0, #2
    b.eq    .Lfrom_el2
    b       .Lat_el1

.Lfrom_el3:
    /* Configure EL3 -> EL2 drop */
    mov     x0, #(1 << 0) | (1 << 8) | (1 << 10) /* NS | HCE | RW */
    msr     scr_el3, x0
    mov     x0, #0x3C9                           /* EL2h, DAIF masked */
    msr     spsr_el3, x0
    adr     x0, .Lfrom_el2
    msr     elr_el3, x0
    eret

.Lfrom_el2:
    /* Enable AArch64 in EL1 (HCR_EL2.RW = 1) */
    mov     x0, #(1 << 31)
    msr     hcr_el2, x0

    /* Allow EL1/EL0 access to physical timer & counter */
    mrs     x0, cnthctl_el2
    orr     x0, x0, #3                           /* EL1PCTEN | EL1PCEN */
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr

    /* Drop to EL1h with interrupts masked */
    mov     x0, #0x3C5                           /* EL1h, DAIF masked */
    msr     spsr_el2, x0
    adr     x0, .Lat_el1
    msr     elr_el2, x0
    eret

.Lat_el1:
    /* Enable Floating Point & SIMD in EL1 and EL0 (CPACR_EL1.FPEN = 0b11) */
    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0
    isb

    /* Load Exception Vector Table Base Address (VBAR_EL1) */
    ldr     x0, =exception_vector_table
    msr     vbar_el1, x0
    isb

    /* Set up initial EL1 stack pointer */
    ldr     x0, =__boot_stack_top
    mov     sp, x0

    /* Zero out .bss section */
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
.Lbss_loop:
    cmp     x0, x1
    b.hs    .Lbss_done
    str     xzr, [x0], #8
    b       .Lbss_loop

.Lbss_done:
    /* Enable EL0 reading of physical counter (CNTPCT_EL0) for cycle-accurate benchmarks */
    mov     x0, #1                               /* EL0PCTEN = 1 */
    msr     cntkctl_el1, x0
    isb

    /* Jump to C microkernel entry point */
    bl      kernel_main

.Lpark_cpu:
    wfe
    b       .Lpark_cpu

/* ============================================================================
 * cpu_switch_to(cpu_context_t *prev, cpu_context_t *next)
 * Saves callee-saved registers into *prev and restores from *next
 * ============================================================================ */
.section ".text"
.global cpu_switch_to
.align 4
cpu_switch_to:
    stp     x19, x20, [x0, #0]
    stp     x21, x22, [x0, #16]
    stp     x23, x24, [x0, #32]
    stp     x25, x26, [x0, #48]
    stp     x27, x28, [x0, #64]
    stp     x29, x30, [x0, #80]
    mov     x9, sp
    str     x9, [x0, #96]

    ldp     x19, x20, [x1, #0]
    ldp     x21, x22, [x1, #16]
    ldp     x23, x24, [x1, #32]
    ldp     x25, x26, [x1, #48]
    ldp     x27, x28, [x1, #64]
    ldp     x29, x30, [x1, #80]
    ldr     x9, [x1, #96]
    mov     sp, x9
    ret
