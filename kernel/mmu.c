#include "mmu.h"
#include "pmm.h"
#include "kprintf.h"

/* Linker symbols for fine-grained section permissions */
extern char __text_start[];
extern char __rodata_end[];
extern char __data_start[];

static vspace_t g_kernel_vspace;

/* Extract L1, L2, L3 indices from a 39-bit Virtual Address (4KB granule) */
ALWAYS_INLINE uint64_t va_l1_idx(uintptr_t va) { return (va >> 30) & 0x1FFULL; }
ALWAYS_INLINE uint64_t va_l2_idx(uintptr_t va) { return (va >> 21) & 0x1FFULL; }
ALWAYS_INLINE uint64_t va_l3_idx(uintptr_t va) { return (va >> 12) & 0x1FFULL; }

static uint64_t flags_to_pte_attrs(uint32_t flags, bool is_user_asid) {
    uint64_t attr = PTE_VALID | PTE_PAGE | PTE_AF;

    if (flags & VM_FLAG_DEVICE) {
        attr |= PTE_ATTRINDX(MT_DEVICE_nGnRnE) | PTE_SH_NONE | PTE_PXN | PTE_UXN;
    } else {
        attr |= PTE_ATTRINDX(MT_NORMAL_WB) | PTE_SH_INNER;
    }

    /* Access Permissions (AP[2:1]) */
    if (flags & VM_FLAG_USER) {
        if (flags & VM_FLAG_WRITE) {
            attr |= PTE_AP_EL0_RW;
        } else {
            attr |= PTE_AP_EL0_RO;
        }
        if (is_user_asid) {
            attr |= PTE_NG;
        }
    } else {
        if (flags & VM_FLAG_WRITE) {
            attr |= PTE_AP_EL1_RW;
        } else {
            attr |= PTE_AP_EL1_RO;
        }
    }

    /* Execute Never Permissions (W^X enforcement) */
    if (!(flags & VM_FLAG_EXEC)) {
        attr |= PTE_UXN;
        if (!(flags & VM_FLAG_USER)) {
            attr |= PTE_PXN;
        }
    } else {
        /* Executable page: if user-executable, allow UXN=0 */
        if (!(flags & VM_FLAG_USER)) {
            attr |= PTE_UXN;
        }
    }

    return attr;
}

int mmu_map_page(vspace_t *vs, uintptr_t vaddr, uintptr_t paddr, uint32_t flags) {
    if (!vs || !vs->l1_table) return EINVAL;

    uint64_t l1_i = va_l1_idx(vaddr);
    uint64_t l2_i = va_l2_idx(vaddr);
    uint64_t l3_i = va_l3_idx(vaddr);

    /* Ensure L2 table exists */
    uint64_t *l2_table;
    if (!(vs->l1_table[l1_i] & PTE_VALID)) {
        uintptr_t l2_phys = pmm_alloc_page();
        if (!l2_phys) return ENOMEM;
        vs->l1_table[l1_i] = (l2_phys & PTE_ADDR_MASK) | PTE_VALID | PTE_TABLE;
        vs->mapped_pages++;
        l2_table = (uint64_t *)l2_phys;
    } else {
        l2_table = (uint64_t *)(vs->l1_table[l1_i] & PTE_ADDR_MASK);
    }

    /* Ensure L3 table exists */
    uint64_t *l3_table;
    if (!(l2_table[l2_i] & PTE_VALID)) {
        uintptr_t l3_phys = pmm_alloc_page();
        if (!l3_phys) return ENOMEM;
        l2_table[l2_i] = (l3_phys & PTE_ADDR_MASK) | PTE_VALID | PTE_TABLE;
        vs->mapped_pages++;
        l3_table = (uint64_t *)l3_phys;
    } else {
        l3_table = (uint64_t *)(l2_table[l2_i] & PTE_ADDR_MASK);
    }

    uint64_t pte_attrs = flags_to_pte_attrs(flags, vs->asid != 0);
    l3_table[l3_i] = (paddr & PTE_ADDR_MASK) | pte_attrs;
    return EOK;
}

int mmu_map_range(vspace_t *vs, uintptr_t vaddr, uintptr_t paddr, size_t size, uint32_t flags) {
    uintptr_t va = ALIGN_DOWN(vaddr, PAGE_SIZE);
    uintptr_t pa = ALIGN_DOWN(paddr, PAGE_SIZE);
    uintptr_t end = ALIGN_UP(vaddr + size, PAGE_SIZE);

    while (va < end) {
        int rc = mmu_map_page(vs, va, pa, flags);
        if (rc != EOK) return rc;
        va += PAGE_SIZE;
        pa += PAGE_SIZE;
    }
    return EOK;
}

uintptr_t mmu_virt_to_phys(vspace_t *vs, uintptr_t vaddr, uint64_t *out_pte) {
    if (!vs || !vs->l1_table) return 0;
    uint64_t l1_e = vs->l1_table[va_l1_idx(vaddr)];
    if (!(l1_e & PTE_VALID)) return 0;

    uint64_t *l2_table = (uint64_t *)(l1_e & PTE_ADDR_MASK);
    uint64_t l2_e = l2_table[va_l2_idx(vaddr)];
    if (!(l2_e & PTE_VALID)) return 0;

    uint64_t *l3_table = (uint64_t *)(l2_e & PTE_ADDR_MASK);
    uint64_t l3_e = l3_table[va_l3_idx(vaddr)];
    if (!(l3_e & PTE_VALID)) return 0;

    if (out_pte) *out_pte = l3_e;
    return (l3_e & PTE_ADDR_MASK) | (vaddr & (PAGE_SIZE - 1));
}

/*
 * Populates base kernel + code mappings into a virtual address space:
 * - MMIO (GICv2 + UART0): EL1 Device RW (EL0 No Access unless explicitly granted)
 * - Code & Read-Only Data (__text_start .. __rodata_end): RX/RO for EL1 & EL0
 * - Kernel Data, BSS, Stacks, and Page Pool (__data_start .. PHYS_RAM_END):
 *   Strictly EL1 RW ONLY (AP_EL1_RW | UXN). Any EL0 access faults immediately!
 */
static int populate_base_mappings(vspace_t *vs) {
    int rc;

    /* Map GICv2 Distributor & CPU Interface (EL1 only) */
    rc = mmu_map_range(vs, VIRT_GICD_BASE, VIRT_GICD_BASE, 0x20000, VM_DEVICE_RW);
    if (rc != EOK) return rc;

    /* Map PL011 UART0 (EL1 only by default; UART driver task gets EL0 grant) */
    rc = mmu_map_page(vs, VIRT_UART0_BASE, VIRT_UART0_BASE, VM_DEVICE_RW);
    if (rc != EOK) return rc;

    /* Map Code + Read-Only Data as Read/Execute (Write-Protected!) */
    uintptr_t text_start = (uintptr_t)__text_start;
    uintptr_t ro_end     = (uintptr_t)__rodata_end;
    rc = mmu_map_range(vs, text_start, text_start, ro_end - text_start, VM_USER_RX);
    if (rc != EOK) return rc;

    /* Map all Kernel Data, BSS, Boot Stack, and PMM Page Pool as EL1-ONLY RW! */
    uintptr_t data_start = (uintptr_t)__data_start;
    rc = mmu_map_range(vs, data_start, data_start, PHYS_RAM_END - data_start, VM_KERNEL_RW);
    if (rc != EOK) return rc;

    return EOK;
}

int mmu_create_vspace(vspace_t *vs, uint16_t asid) {
    if (!vs) return EINVAL;
    uintptr_t l1_phys = pmm_alloc_page();
    if (!l1_phys) return ENOMEM;

    vs->l1_table     = (uint64_t *)l1_phys;
    vs->asid         = asid;
    vs->mapped_pages = 1;

    return populate_base_mappings(vs);
}

void mmu_switch_vspace(vspace_t *vs) {
    if (!vs || !vs->l1_table) return;
    uint64_t ttbr0 = ((uint64_t)vs->asid << 48) | ((uintptr_t)vs->l1_table & PTE_ADDR_MASK);
    __asm__ volatile(
        "msr ttbr0_el1, %0\n\t"
        "isb"
        :: "r"(ttbr0)
        : "memory"
    );
}

vspace_t *mmu_get_kernel_vspace(void) {
    return &g_kernel_vspace;
}

void mmu_init_kernel(void) {
    int rc = mmu_create_vspace(&g_kernel_vspace, 0);
    if (rc != EOK) {
        kpanic("mmu_init_kernel: failed to build kernel page tables (%d)", rc);
    }

    /*
     * Configure MAIR_EL1:
     * Attr0 = 0x00 (Device-nGnRnE)
     * Attr1 = 0xFF (Normal Memory, Inner/Outer Write-Back Non-Transient Read/Write Allocate)
     */
    uint64_t mair = (0x00ULL << (MT_DEVICE_nGnRnE * 8)) |
                    (0xFFULL << (MT_NORMAL_WB * 8));
    __asm__ volatile("msr mair_el1, %0" :: "r"(mair));

    /*
     * Configure TCR_EL1 for 39-bit Virtual Address Space (T0SZ=25, T1SZ=25),
     * 4KB Granule (TG0=0b00, TG1=0b10), Inner Shareable, Write-Back Cacheable,
     * 16-bit ASID, 40-bit IPS (0b010 = 1TB Physical Address Space).
     */
    uint64_t tcr = (25ULL << 0)  |  /* T0SZ = 25 (39-bit VA, 512GB, L1->L2->L3) */
                   (1ULL  << 8)  |  /* IRGN0 = Inner WBWA */
                   (1ULL  << 10) |  /* ORGN0 = Outer WBWA */
                   (3ULL  << 12) |  /* SH0   = Inner Shareable */
                   (0ULL  << 14) |  /* TG0   = 4KB Granule */
                   (25ULL << 16) |  /* T1SZ  = 25 */
                   (2ULL  << 30) |  /* TG1   = 4KB Granule */
                   (2ULL  << 32);   /* IPS   = 40-bit Physical Address */
    __asm__ volatile("msr tcr_el1, %0" :: "r"(tcr));

    /* Load kernel L1 page table into TTBR0_EL1 & TTBR1_EL1 */
    uint64_t ttbr = (uintptr_t)g_kernel_vspace.l1_table & PTE_ADDR_MASK;
    __asm__ volatile(
        "msr ttbr0_el1, %0\n\t"
        "msr ttbr1_el1, %0\n\t"
        "dsb ish\n\t"
        "tlbi vmalle1is\n\t"
        "dsb ish\n\t"
        "isb"
        :: "r"(ttbr)
        : "memory"
    );

    /* Enable MMU, Data Cache (C), and Instruction Cache (I) in SCTLR_EL1 */
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ULL << 0)  | /* M: MMU Enable */
             (1ULL << 2)  | /* C: Data Cache Enable */
             (1ULL << 12);  /* I: Instruction Cache Enable */
    sctlr &= ~(1ULL << 1);  /* A: Disable strict alignment fault for normal memory */
    __asm__ volatile(
        "msr sctlr_el1, %0\n\t"
        "dsb sy\n\t"
        "isb"
        :: "r"(sctlr)
        : "memory"
    );
}
