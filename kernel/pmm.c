#include "pmm.h"
#include "kprintf.h"

/* Maximum physical pages we track in our bitmap (128 MB / 4 KB = 32,768 pages) */
#define MAX_PHYS_PAGES      (PHYS_RAM_SIZE / PAGE_SIZE)
#define BITMAP_WORDS        (MAX_PHYS_PAGES / 64)

static uint64_t  g_page_bitmap[BITMAP_WORDS];
static uintptr_t g_pool_start;
static uintptr_t g_pool_end;
static uint64_t  g_total_pages;
static uint64_t  g_used_pages;
static uint64_t  g_search_hint;

ALWAYS_INLINE void bitmap_set(uint64_t idx) {
    g_page_bitmap[idx >> 6] |= (1ULL << (idx & 63));
}

ALWAYS_INLINE void bitmap_clear(uint64_t idx) {
    g_page_bitmap[idx >> 6] &= ~(1ULL << (idx & 63));
}

ALWAYS_INLINE bool bitmap_test(uint64_t idx) {
    return (g_page_bitmap[idx >> 6] & (1ULL << (idx & 63))) != 0;
}

void pmm_init(uintptr_t free_ram_start, uintptr_t free_ram_end) {
    g_pool_start  = ALIGN_UP(free_ram_start, PAGE_SIZE);
    g_pool_end    = ALIGN_DOWN(free_ram_end, PAGE_SIZE);
    g_total_pages = (g_pool_end - g_pool_start) / PAGE_SIZE;
    if (g_total_pages > MAX_PHYS_PAGES) {
        g_total_pages = MAX_PHYS_PAGES;
        g_pool_end = g_pool_start + g_total_pages * PAGE_SIZE;
    }

    memset(g_page_bitmap, 0, sizeof(g_page_bitmap));
    g_used_pages  = 0;
    g_search_hint = 0;
}

uintptr_t pmm_alloc_page(void) {
    uint64_t flags = irq_save();
    uint64_t total_words = (g_total_pages + 63) / 64;

    for (uint64_t step = 0; step < total_words; step++) {
        uint64_t w_idx = (g_search_hint + step) % total_words;
        uint64_t word = g_page_bitmap[w_idx];
        if (word != ~0ULL) {
            uint64_t bit = (uint64_t)__builtin_ctzll(~word);
            uint64_t page_idx = (w_idx << 6) + bit;
            if (page_idx < g_total_pages) {
                bitmap_set(page_idx);
                g_used_pages++;
                g_search_hint = w_idx;
                uintptr_t paddr = g_pool_start + (page_idx << PAGE_SHIFT);
                irq_restore(flags);
                memset((void *)paddr, 0, PAGE_SIZE);
                return paddr;
            }
        }
    }

    irq_restore(flags);
    return 0;
}

uintptr_t pmm_alloc_pages(size_t count) {
    if (count == 0) return 0;
    if (count == 1) return pmm_alloc_page();

    uint64_t flags = irq_save();
    uint64_t run = 0;
    uint64_t start_idx = 0;

    for (uint64_t i = 0; i < g_total_pages; i++) {
        if (!bitmap_test(i)) {
            if (run == 0) start_idx = i;
            run++;
            if (run == count) {
                for (uint64_t j = start_idx; j < start_idx + count; j++) {
                    bitmap_set(j);
                }
                g_used_pages += count;
                uintptr_t paddr = g_pool_start + (start_idx << PAGE_SHIFT);
                irq_restore(flags);
                memset((void *)paddr, 0, count * PAGE_SIZE);
                return paddr;
            }
        } else {
            run = 0;
        }
    }

    irq_restore(flags);
    return 0;
}

void pmm_free_page(uintptr_t phys_addr) {
    if (phys_addr < g_pool_start || phys_addr >= g_pool_end) {
        return;
    }
    uint64_t flags = irq_save();
    uint64_t page_idx = (phys_addr - g_pool_start) >> PAGE_SHIFT;
    if (bitmap_test(page_idx)) {
        bitmap_clear(page_idx);
        g_used_pages--;
        uint64_t w_idx = page_idx >> 6;
        if (w_idx < g_search_hint) {
            g_search_hint = w_idx;
        }
    }
    irq_restore(flags);
}

void pmm_get_stats(pmm_stats_t *out) {
    if (!out) return;
    uint64_t flags = irq_save();
    out->total_pages = g_total_pages;
    out->used_pages  = g_used_pages;
    out->free_pages  = g_total_pages - g_used_pages;
    out->pool_start  = g_pool_start;
    out->pool_end    = g_pool_end;
    irq_restore(flags);
}
