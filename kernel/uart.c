#include "uart.h"
#include "arch.h"

#define RX_RING_SIZE 256

static volatile char     g_rx_ring[RX_RING_SIZE];
static volatile uint32_t g_rx_head = 0;
static volatile uint32_t g_rx_tail = 0;

void uart_init(void) {
    /* Disable UART before configuration */
    mmio_write32(VIRT_UART0_BASE + UART_CR, 0x0);

    /* Clear all pending UART interrupts */
    mmio_write32(VIRT_UART0_BASE + UART_ICR, 0x7FF);

    /* Set baud rate divisor (115200 baud with 24MHz UARTCLK) */
    mmio_write32(VIRT_UART0_BASE + UART_IBRD, 13);
    mmio_write32(VIRT_UART0_BASE + UART_FBRD, 1);

    /* 8 bits, no parity, 1 stop bit, enable FIFOs (FEN=1, WLEN=0b11) */
    mmio_write32(VIRT_UART0_BASE + UART_LCR_H, (1U << 4) | (3U << 5));

    /* Enable RX and RX-timeout interrupts (IRQ 33) */
    mmio_write32(VIRT_UART0_BASE + UART_IMSC, UART_IMSC_RXIM | UART_IMSC_RTIM);

    /* Enable UART, TX, and RX (UARTEN=1, TXE=1, RXE=1) */
    mmio_write32(VIRT_UART0_BASE + UART_CR, (1U << 0) | (1U << 8) | (1U << 9));
}

void uart_putc(char c) {
    if (c == '\n') {
        while (mmio_read32(VIRT_UART0_BASE + UART_FR) & UART_FR_TXFF) {
            /* spin until TX FIFO has space */
        }
        mmio_write32(VIRT_UART0_BASE + UART_DR, '\r');
    }
    while (mmio_read32(VIRT_UART0_BASE + UART_FR) & UART_FR_TXFF) {
        /* spin until TX FIFO has space */
    }
    mmio_write32(VIRT_UART0_BASE + UART_DR, (uint32_t)(uint8_t)c);
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

int uart_getc_nonblock(void) {
    if (!(mmio_read32(VIRT_UART0_BASE + UART_FR) & UART_FR_RXFE)) {
        return (int)(mmio_read32(VIRT_UART0_BASE + UART_DR) & 0xFFU);
    }
    return -1;
}

char uart_getc_blocking(void) {
    for (;;) {
        int c = uart_rx_pop();
        if (c >= 0) {
            return (char)c;
        }
        c = uart_getc_nonblock();
        if (c >= 0) {
            return (char)c;
        }
        wfi();
    }
}

void uart_handle_irq(void) {
    while (!(mmio_read32(VIRT_UART0_BASE + UART_FR) & UART_FR_RXFE)) {
        char c = (char)(mmio_read32(VIRT_UART0_BASE + UART_DR) & 0xFFU);
        uint32_t next = (g_rx_head + 1) % RX_RING_SIZE;
        if (next != g_rx_tail) {
            g_rx_ring[g_rx_head] = c;
            g_rx_head = next;
        }
    }
    mmio_write32(VIRT_UART0_BASE + UART_ICR, UART_IMSC_RXIM | UART_IMSC_RTIM);
}

bool uart_rx_available(void) {
    if (g_rx_head != g_rx_tail) {
        return true;
    }
    return !(mmio_read32(VIRT_UART0_BASE + UART_FR) & UART_FR_RXFE);
}

int uart_rx_pop(void) {
    uint64_t flags = irq_save();
    int res = -1;
    if (g_rx_head != g_rx_tail) {
        res = (uint8_t)g_rx_ring[g_rx_tail];
        g_rx_tail = (g_rx_tail + 1) % RX_RING_SIZE;
    } else if (!(mmio_read32(VIRT_UART0_BASE + UART_FR) & UART_FR_RXFE)) {
        res = (int)(mmio_read32(VIRT_UART0_BASE + UART_DR) & 0xFFU);
    }
    irq_restore(flags);
    return res;
}
