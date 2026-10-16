#include "gic.h"
#include "arch.h"

/* GICv2 Distributor Registers (Offsets from VIRT_GICD_BASE = 0x08000000) */
#define GICD_CTLR           0x000
#define GICD_ISENABLER      0x100
#define GICD_ICENABLER      0x180
#define GICD_ICPENDR        0x280
#define GICD_IPRIORITYR     0x400
#define GICD_ITARGETSR      0x800
#define GICD_ICFGR          0xC00

/* GICv2 CPU Interface Registers (Offsets from VIRT_GICC_BASE = 0x08010000) */
#define GICC_CTLR           0x000
#define GICC_PMR            0x004
#define GICC_BPR            0x008
#define GICC_IAR            0x00C
#define GICC_EOIR           0x010

void gic_init(void) {
    /* Disable Distributor and CPU Interface while configuring */
    mmio_write32(VIRT_GICD_BASE + GICD_CTLR, 0x0);
    mmio_write32(VIRT_GICC_BASE + GICC_CTLR, 0x0);

    /* Allow all priority levels through CPU Interface (PMR = 0xFF) */
    mmio_write32(VIRT_GICC_BASE + GICC_PMR, 0xFF);
    mmio_write32(VIRT_GICC_BASE + GICC_BPR, 0x0);

    /* Enable Distributor and CPU Interface (Group 0 + Group 1) */
    mmio_write32(VIRT_GICD_BASE + GICD_CTLR, 0x1);
    mmio_write32(VIRT_GICC_BASE + GICC_CTLR, 0x1);
    dsb_sy();
}

void gic_enable_irq(uint32_t irq_id, uint8_t priority) {
    /* Set priority byte */
    uintptr_t prio_reg = VIRT_GICD_BASE + GICD_IPRIORITYR + (irq_id & ~3U);
    uint32_t prio_shift = (irq_id & 3U) * 8;
    uint32_t val = mmio_read32(prio_reg);
    val &= ~(0xFFU << prio_shift);
    val |= ((uint32_t)priority << prio_shift);
    mmio_write32(prio_reg, val);

    /* Route SPI (irq_id >= 32) to CPU 0 (target mask = 0x01) */
    if (irq_id >= 32) {
        uintptr_t targ_reg = VIRT_GICD_BASE + GICD_ITARGETSR + (irq_id & ~3U);
        uint32_t targ_shift = (irq_id & 3U) * 8;
        uint32_t tval = mmio_read32(targ_reg);
        tval &= ~(0xFFU << targ_shift);
        tval |= (0x01U << targ_shift);
        mmio_write32(targ_reg, tval);
    }

    /* Enable IRQ in GICD_ISENABLER */
    uintptr_t en_reg = VIRT_GICD_BASE + GICD_ISENABLER + ((irq_id / 32) * 4);
    mmio_write32(en_reg, 1U << (irq_id % 32));
    dsb_sy();
}

uint32_t gic_ack_irq(void) {
    return mmio_read32(VIRT_GICC_BASE + GICC_IAR);
}

void gic_eoi_irq(uint32_t iar_val) {
    mmio_write32(VIRT_GICC_BASE + GICC_EOIR, iar_val);
}
