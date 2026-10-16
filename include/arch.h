#ifndef UKERNEL_ARCH_H
#define UKERNEL_ARCH_H

#include "types.h"

/* ============================================================================
 * QEMU 'virt' Machine Physical Memory Map (AArch64)
 * ============================================================================ */
#define VIRT_GICD_BASE      0x08000000ULL
#define VIRT_GICC_BASE      0x08010000ULL
#define VIRT_UART0_BASE     0x09000000ULL
#define VIRT_MMIO_BASE      0x08000000ULL
#define VIRT_MMIO_SIZE      0x02000000ULL   /* 32 MB covers GIC + UART + VirtIO */

#define PHYS_RAM_BASE       0x40000000ULL
#define PHYS_RAM_SIZE       (128ULL * 1024 * 1024) /* 128 MB RAM */
#define PHYS_RAM_END        (PHYS_RAM_BASE + PHYS_RAM_SIZE)

#define PAGE_SHIFT          12
#define PAGE_SIZE           (1ULL << PAGE_SHIFT)   /* 4096 bytes */
#define PAGE_MASK           (~(PAGE_SIZE - 1ULL))

/* ============================================================================
 * ARM64 Exception Syndrome Register (ESR_EL1) Constants
 * ============================================================================ */
#define ESR_EC_SHIFT        26
#define ESR_EC_MASK         0x3FULL
#define ESR_ISS_MASK        0x01FFFFFFULL

#define ESR_EC_UNKNOWN      0x00
#define ESR_EC_SVC64        0x15  /* SVC instruction execution in AArch64 */
#define ESR_EC_IABT_EL0     0x20  /* Instruction Abort from lower EL (EL0) */
#define ESR_EC_IABT_EL1     0x21  /* Instruction Abort from current EL (EL1) */
#define ESR_EC_PC_ALIGN     0x22  /* PC alignment fault */
#define ESR_EC_DABT_EL0     0x24  /* Data Abort from lower EL (EL0) */
#define ESR_EC_DABT_EL1     0x25  /* Data Abort from current EL (EL1) */
#define ESR_EC_SP_ALIGN     0x26  /* SP alignment fault */

/* ============================================================================
 * SPSR_EL1 Mode Constants
 * ============================================================================ */
#define SPSR_EL0t           0x00000000ULL  /* EL0 using SP_EL0, all interrupts enabled */
#define SPSR_EL1h           0x00000005ULL  /* EL1 using SP_EL1, all interrupts enabled */
#define SPSR_IRQ_MASK       (1ULL << 7)
#define SPSR_FIQ_MASK       (1ULL << 6)
#define SPSR_ASYNC_MASK     (1ULL << 8)
#define SPSR_DEBUG_MASK     (1ULL << 9)

/* ============================================================================
 * Saved CPU Trap Frame (272 bytes, 16-byte aligned)
 * Pushed onto the current task's kernel stack upon any EL1/EL0 exception
 * ============================================================================ */
typedef struct trap_frame {
    uint64_t x[31];         /* x0 .. x30 (x30 = LR) */
    uint64_t sp_el0;        /* User stack pointer (SP_EL0) */
    uint64_t elr_el1;       /* Exception Link Register (return PC) */
    uint64_t spsr_el1;      /* Saved Program Status Register */
} ALIGNED(16) trap_frame_t;

/* ============================================================================
 * Callee-Saved CPU Context for Kernel Thread Switching
 * ============================================================================ */
typedef struct cpu_context {
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t x29; /* Frame Pointer */
    uint64_t x30; /* Link Register (Return PC) */
    uint64_t sp;  /* Kernel Stack Pointer (SP_EL1) */
} cpu_context_t;

/* ============================================================================
 * Inline Assembly Helpers for AArch64 System Registers & Barriers
 * ============================================================================ */
ALWAYS_INLINE void isb(void) {
    __asm__ volatile("isb" ::: "memory");
}

ALWAYS_INLINE void dsb_sy(void) {
    __asm__ volatile("dsb sy" ::: "memory");
}

ALWAYS_INLINE void dsb_ish(void) {
    __asm__ volatile("dsb ish" ::: "memory");
}

ALWAYS_INLINE void dmb_ish(void) {
    __asm__ volatile("dmb ish" ::: "memory");
}

ALWAYS_INLINE void wfi(void) {
    __asm__ volatile("wfi" ::: "memory");
}

ALWAYS_INLINE void irq_enable(void) {
    __asm__ volatile("msr daifclr, #2" ::: "memory");
}

ALWAYS_INLINE void irq_disable(void) {
    __asm__ volatile("msr daifset, #2" ::: "memory");
}

ALWAYS_INLINE uint64_t irq_save(void) {
    uint64_t flags;
    __asm__ volatile(
        "mrs %0, daif\n\t"
        "msr daifset, #2"
        : "=r"(flags)
        :
        : "memory"
    );
    return flags;
}

ALWAYS_INLINE void irq_restore(uint64_t flags) {
    __asm__ volatile("msr daif, %0" :: "r"(flags) : "memory");
}

ALWAYS_INLINE uint64_t read_current_el(void) {
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    return (el >> 2) & 0x3;
}

ALWAYS_INLINE uint64_t read_cntfrq(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

ALWAYS_INLINE uint64_t read_cntpct(void) {
    uint64_t val;
    __asm__ volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(val));
    return val;
}

ALWAYS_INLINE uint64_t read_esr_el1(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(val));
    return val;
}

ALWAYS_INLINE uint64_t read_far_el1(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, far_el1" : "=r"(val));
    return val;
}

ALWAYS_INLINE uint64_t read_elr_el1(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, elr_el1" : "=r"(val));
    return val;
}

/* MMIO Read/Write Primitives */
ALWAYS_INLINE void mmio_write32(uintptr_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}

ALWAYS_INLINE uint32_t mmio_read32(uintptr_t addr) {
    return *(volatile uint32_t *)addr;
}

/* PSCI System Off (cleanly exits QEMU) */
ALWAYS_INLINE NORETURN void psci_system_off(void) {
    register uint64_t x0 __asm__("x0") = 0x84000008ULL; /* PSCI_0_2_FN_SYSTEM_OFF */
    __asm__ volatile("hvc #0" :: "r"(x0) : "memory");
    for (;;) {
        wfi();
    }
}

#endif /* UKERNEL_ARCH_H */
