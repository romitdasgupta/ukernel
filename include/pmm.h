#ifndef UKERNEL_PMM_H
#define UKERNEL_PMM_H

#include "types.h"
#include "arch.h"

typedef struct pmm_stats {
    uint64_t total_pages;
    uint64_t used_pages;
    uint64_t free_pages;
    uintptr_t pool_start;
    uintptr_t pool_end;
} pmm_stats_t;

void      pmm_init(uintptr_t free_ram_start, uintptr_t free_ram_end);
uintptr_t pmm_alloc_page(void);
uintptr_t pmm_alloc_pages(size_t count);
void      pmm_free_page(uintptr_t phys_addr);
void      pmm_get_stats(pmm_stats_t *out);

#endif /* UKERNEL_PMM_H */
