#ifndef UKERNEL_MMU_H
#define UKERNEL_MMU_H

#include "types.h"
#include "arch.h"

/* ============================================================================
 * ARMv8-A 4KB Granule Page Table Definitions (39-bit VA, 3-Level: L1 -> L2 -> L3)
 * ============================================================================ */
#define PTE_VALID           (1ULL << 0)
#define PTE_TABLE           (1ULL << 1) /* For L1/L2 table descriptors */
#define PTE_PAGE            (1ULL << 1) /* For L3 4KB page descriptors */
#define PTE_BLOCK           (0ULL << 1) /* For L1 (1GB) / L2 (2MB) block descriptors */

/* Memory Attribute Indirection Register (MAIR_EL1) indices */
#define MT_DEVICE_nGnRnE    0ULL
#define MT_NORMAL_WB        1ULL
#define PTE_ATTRINDX(idx)   (((uint64_t)(idx) & 0x7ULL) << 2)

/* Access Permissions (AP[2:1] in bits [7:6]) */
#define PTE_AP_EL1_RW       (0ULL << 6) /* EL1 RW, EL0 No Access */
#define PTE_AP_EL0_RW       (1ULL << 6) /* EL1 RW, EL0 RW */
#define PTE_AP_EL1_RO       (2ULL << 6) /* EL1 RO, EL0 No Access */
#define PTE_AP_EL0_RO       (3ULL << 6) /* EL1 RO, EL0 RO */

/* Shareability (SH[1:0] in bits [9:8]) */
#define PTE_SH_NONE         (0ULL << 8)
#define PTE_SH_OUTER        (2ULL << 8)
#define PTE_SH_INNER        (3ULL << 8)

/* Access Flag (bit 10) - MUST be 1 or CPU triggers Access Flag Fault */
#define PTE_AF              (1ULL << 10)

/* Non-Global bit (bit 11) - 1 for ASID-tagged process mappings */
#define PTE_NG              (1ULL << 11)

/* Execute-Never bits */
#define PTE_PXN             (1ULL << 53) /* Privileged Execute Never */
#define PTE_UXN             (1ULL << 54) /* Unprivileged (EL0) Execute Never */

#define PTE_ADDR_MASK       0x0000FFFFFFFFF000ULL

/* High-level mapping permission flags */
#define VM_FLAG_READ        (1U << 0)
#define VM_FLAG_WRITE       (1U << 1)
#define VM_FLAG_EXEC        (1U << 2)
#define VM_FLAG_USER        (1U << 3)
#define VM_FLAG_DEVICE      (1U << 4)

#define VM_KERNEL_RW        (VM_FLAG_READ | VM_FLAG_WRITE)
#define VM_KERNEL_RX        (VM_FLAG_READ | VM_FLAG_EXEC)
#define VM_KERNEL_RWX       (VM_FLAG_READ | VM_FLAG_WRITE | VM_FLAG_EXEC)
#define VM_DEVICE_RW        (VM_FLAG_READ | VM_FLAG_WRITE | VM_FLAG_DEVICE)
#define VM_USER_RW          (VM_FLAG_READ | VM_FLAG_WRITE | VM_FLAG_USER)
#define VM_USER_RX          (VM_FLAG_READ | VM_FLAG_EXEC | VM_FLAG_USER)
#define VM_USER_RWX         (VM_FLAG_READ | VM_FLAG_WRITE | VM_FLAG_EXEC | VM_FLAG_USER)
#define VM_USER_DEVICE      (VM_FLAG_READ | VM_FLAG_WRITE | VM_FLAG_USER | VM_FLAG_DEVICE)

/* Per-Task Virtual Address Space (VSpace) */
typedef struct vspace {
    uint64_t *l1_table;     /* Virtual/Physical pointer to 4KB L1 root page table */
    uint16_t  asid;         /* Hardware Address Space ID (0 = kernel, 1..255 = tasks) */
    uint32_t  mapped_pages; /* Count of L2/L3 tables + private pages mapped */
} vspace_t;

void mmu_init_kernel(void);
int  mmu_create_vspace(vspace_t *vs, uint16_t asid);
int  mmu_map_page(vspace_t *vs, uintptr_t vaddr, uintptr_t paddr, uint32_t flags);
int  mmu_map_range(vspace_t *vs, uintptr_t vaddr, uintptr_t paddr, size_t size, uint32_t flags);
void mmu_switch_vspace(vspace_t *vs);
uintptr_t mmu_virt_to_phys(vspace_t *vs, uintptr_t vaddr, uint64_t *out_pte);
vspace_t *mmu_get_kernel_vspace(void);

#endif /* UKERNEL_MMU_H */
