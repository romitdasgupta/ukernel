#ifndef UKERNEL_UART_H
#define UKERNEL_UART_H

#include "types.h"

/* PL011 UART Registers (Offsets from VIRT_UART0_BASE = 0x09000000) */
#define UART_DR             0x00    /* Data Register */
#define UART_FR             0x18    /* Flag Register */
#define UART_IBRD           0x24    /* Integer Baud Rate Register */
#define UART_FBRD           0x28    /* Fractional Baud Rate Register */
#define UART_LCR_H          0x2C    /* Line Control Register */
#define UART_CR             0x30    /* Control Register */
#define UART_IMSC           0x38    /* Interrupt Mask Set/Clear Register */
#define UART_ICR            0x44    /* Interrupt Clear Register */

/* Flag Register Bits */
#define UART_FR_RXFE        (1U << 4) /* Receive FIFO Empty */
#define UART_FR_TXFF        (1U << 5) /* Transmit FIFO Full */
#define UART_FR_BUSY        (1U << 3) /* UART Busy */

/* Interrupt Mask Bits */
#define UART_IMSC_RXIM      (1U << 4) /* Receive Interrupt Mask */
#define UART_IMSC_RTIM      (1U << 6) /* Receive Timeout Interrupt Mask */

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
int  uart_getc_nonblock(void);
char uart_getc_blocking(void);
void uart_handle_irq(void);

/* Ring-buffer access for RX characters captured by IRQ 33 */
int  uart_rx_pop(void);
bool uart_rx_available(void);

#endif /* UKERNEL_UART_H */
