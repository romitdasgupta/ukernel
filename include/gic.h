#ifndef UKERNEL_GIC_H
#define UKERNEL_GIC_H

#include "types.h"
#include "arch.h"

/* ARM GICv2 Interrupt IDs on QEMU 'virt' */
#define IRQ_TIMER_PHYS      30  /* Non-secure Physical Timer PPI (ID 30) */
#define IRQ_UART0           33  /* PL011 UART0 SPI (ID 33) */
#define GIC_SPURIOUS_IRQ    1023

void     gic_init(void);
void     gic_enable_irq(uint32_t irq_id, uint8_t priority);
uint32_t gic_ack_irq(void);
void     gic_eoi_irq(uint32_t iar_val);

#endif /* UKERNEL_GIC_H */
