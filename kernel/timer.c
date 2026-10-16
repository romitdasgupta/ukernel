#include "timer.h"
#include "gic.h"
#include "sched.h"
#include "arch.h"

static uint64_t g_timer_freq     = 0;
static uint64_t g_timer_interval = 0;
static volatile uint64_t g_ticks = 0;

void timer_init(uint32_t hz) {
    g_timer_freq = read_cntfrq();
    if (g_timer_freq == 0) {
        g_timer_freq = 62500000ULL; /* Fallback 62.5 MHz if firmware leaves unset */
    }
    g_timer_interval = g_timer_freq / hz;
    g_ticks = 0;

    /* Program countdown value (CNTP_TVAL_EL0) and enable timer (ENABLE=1, IMASK=0) */
    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(g_timer_interval));
    uint64_t ctl = 1ULL;
    __asm__ volatile("msr cntp_ctl_el0, %0\n\tisb" :: "r"(ctl));

    /* Enable Physical Timer PPI (IRQ 30) in GICv2 */
    gic_enable_irq(IRQ_TIMER_PHYS, 0x80);
}

void timer_handle_irq(void) {
    g_ticks++;
    /* Rearm physical timer for next interval */
    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(g_timer_interval));
    sched_tick();
}

uint64_t timer_get_ticks(void) {
    return g_ticks;
}

uint64_t timer_get_freq(void) {
    return g_timer_freq;
}

uint64_t timer_get_uptime_ms(void) {
    return (g_ticks * 1000ULL) / TIMER_HZ;
}

uint64_t timer_cycles_to_ns(uint64_t cycles) {
    if (g_timer_freq == 0) return 0;
    return (cycles * 1000000000ULL) / g_timer_freq;
}
