/*
 * Helios64 Microkernel - AArch64 Exception Vector Table & Trap Entry/Exit
 * Each vector entry in AArch64 is 128 bytes (0x80), table aligned to 2048 bytes (2^11)
 */

.section ".text.vectors"
.global exception_vector_table
.global ret_from_fork

/* Save all 31 general-purpose registers + SP_EL0 + ELR_EL1 + SPSR_EL1 (272 bytes) */
.macro SAVE_TRAP_FRAME
    sub     sp, sp, #272
    stp     x0,  x1,  [sp, #0]
    stp     x2,  x3,  [sp, #16]
    stp     x4,  x5,  [sp, #32]
    stp     x6,  x7,  [sp, #48]
    stp     x8,  x9,  [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]

    mrs     x9,  sp_el0
    stp     x30, x9,  [sp, #240]

    mrs     x10, elr_el1
    mrs     x11, spsr_el1
    stp     x10, x11, [sp, #256]
.endm

/* Restore all registers + SP_EL0 + ELR_EL1 + SPSR_EL1 and return via ERET */
.macro RESTORE_TRAP_FRAME
    ldp     x10, x11, [sp, #256]
    msr     elr_el1,  x10
    msr     spsr_el1, x11

    ldp     x30, x9,  [sp, #240]
    msr     sp_el0,   x9

    ldp     x28, x29, [sp, #224]
    ldp     x26, x27, [sp, #208]
    ldp     x24, x25, [sp, #192]
    ldp     x22, x23, [sp, #176]
    ldp     x20, x21, [sp, #160]
    ldp     x18, x19, [sp, #144]
    ldp     x16, x17, [sp, #128]
    ldp     x14, x15, [sp, #112]
    ldp     x12, x13, [sp, #96]
    ldp     x10, x11, [sp, #80]
    ldp     x8,  x9,  [sp, #64]
    ldp     x6,  x7,  [sp, #48]
    ldp     x4,  x5,  [sp, #32]
    ldp     x2,  x3,  [sp, #16]
    ldp     x0,  x1,  [sp, #0]
    add     sp, sp, #272
    eret
.endm

.align 11
exception_vector_table:
    /* --------------------------------------------------------------------
     * Current EL with SP0 (EL1t)
     * -------------------------------------------------------------------- */
    .align 7
    b       el1_sync_entry          /* Synchronous */
    .align 7
    b       el1_irq_entry           /* IRQ */
    .align 7
    b       unhandled_vec_entry     /* FIQ */
    .align 7
    b       unhandled_vec_entry     /* SError */

    /* --------------------------------------------------------------------
     * Current EL with SPx (EL1h)
     * -------------------------------------------------------------------- */
    .align 7
    b       el1_sync_entry          /* Synchronous */
    .align 7
    b       el1_irq_entry           /* IRQ */
    .align 7
    b       unhandled_vec_entry     /* FIQ */
    .align 7
    b       unhandled_vec_entry     /* SError */

    /* --------------------------------------------------------------------
     * Lower EL using AArch64 (EL0 -> EL1)
     * -------------------------------------------------------------------- */
    .align 7
    b       el0_sync_entry          /* Synchronous (SVC / Page Fault) */
    .align 7
    b       el0_irq_entry           /* IRQ (Preemptive Timer / UART) */
    .align 7
    b       unhandled_vec_entry     /* FIQ */
    .align 7
    b       unhandled_vec_entry     /* SError */

    /* --------------------------------------------------------------------
     * Lower EL using AArch32
     * -------------------------------------------------------------------- */
    .align 7
    b       unhandled_vec_entry
    .align 7
    b       unhandled_vec_entry
    .align 7
    b       unhandled_vec_entry
    .align 7
    b       unhandled_vec_entry

/* ============================================================================
 * Out-of-line Trap Handlers
 * ============================================================================ */
.align 4
el1_sync_entry:
    SAVE_TRAP_FRAME
    mov     x0, sp
    bl      trap_el1_sync
    RESTORE_TRAP_FRAME

.align 4
el1_irq_entry:
    SAVE_TRAP_FRAME
    mov     x0, sp
    bl      trap_irq_handler
    RESTORE_TRAP_FRAME

.align 4
el0_sync_entry:
    SAVE_TRAP_FRAME
    mov     x0, sp
    bl      trap_el0_sync
    RESTORE_TRAP_FRAME

.align 4
el0_irq_entry:
    SAVE_TRAP_FRAME
    mov     x0, sp
    bl      trap_irq_handler
    RESTORE_TRAP_FRAME

.align 4
unhandled_vec_entry:
    SAVE_TRAP_FRAME
    mov     x0, sp
    bl      trap_unhandled
    RESTORE_TRAP_FRAME

/*
 * First entry point for newly spawned tasks when switched in via cpu_switch_to.
 * If x19 != 0, it's a kernel thread and x19 holds the C function pointer.
 * If x19 == 0, it's an EL0 user task and its initial trap_frame_t is on SP.
 */
.align 4
ret_from_fork:
    cbz     x19, .Lenter_el0
    /* Kernel thread: enable IRQs and call function in x19 */
    msr     daifclr, #2
    blr     x19
    /* If kernel thread returns, park it */
1:  wfi
    b       1b

.Lenter_el0:
    RESTORE_TRAP_FRAME
