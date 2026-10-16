#ifndef UKERNEL_TIMER_H
#define UKERNEL_TIMER_H

#include "types.h"

#define TIMER_HZ            100ULL  /* 100 Hz (10ms preemption quantum) */

void     timer_init(uint32_t hz);
void     timer_handle_irq(void);
uint64_t timer_get_ticks(void);
uint64_t timer_get_freq(void);
uint64_t timer_get_uptime_ms(void);
uint64_t timer_cycles_to_ns(uint64_t cycles);

#endif /* UKERNEL_TIMER_H */
