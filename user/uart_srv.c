#include "user.h"
#include "uart.h"
#include "arch.h"

/*
 * Direct EL0 User-Space PL011 Hardware Access!
 * Because uart_srv (PID 1, ASID 1) holds the MMIO capability and has
 * 0x09000000 mapped with PTE_AP_EL0_RW in its private VSpace, it drives
 * the PL011 TX FIFO directly from unprivileged EL0 without kernel traps!
 */
static inline void el0_uart_hw_putc(char c) {
    volatile uint32_t *uart_dr = (volatile uint32_t *)(VIRT_UART0_BASE + UART_DR);
    volatile uint32_t *uart_fr = (volatile uint32_t *)(VIRT_UART0_BASE + UART_FR);

    if (c == '\n') {
        while (*uart_fr & UART_FR_TXFF) {}
        *uart_dr = '\r';
    }
    while (*uart_fr & UART_FR_TXFF) {}
    *uart_dr = (uint32_t)(uint8_t)c;
}

static inline int el0_uart_console_getc(void) {
    register uint64_t x0 __asm__("x0") = CONSOLE_OP_GETC;
    register uint64_t x8 __asm__("x8") = SYS_DEBUG_CONSOLE;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return (int)(int64_t)x0;
}

void user_uart_server_main(void) {
    uint64_t tx_bytes = 0;
    uint64_t rx_bytes = 0;
    uint64_t reqs     = 0;

    ipc_msg_t msg;
    int rc = sys_ipc_recv(CAP_SLOT_UART_EP, &msg);

    for (;;) {
        if (rc != EOK) {
            rc = sys_ipc_recv(CAP_SLOT_UART_EP, &msg);
            continue;
        }

        reqs++;
        uint64_t reply_tag = EOK;
        uint64_t r0 = 0, r1 = 0, r2 = 0;

        switch (msg.tag) {
            case UART_IPC_PUTC: {
                char c = (char)(msg.mr[0] & 0xFFU);
                el0_uart_hw_putc(c);
                tx_bytes++;
                break;
            }
            case UART_IPC_WRITE_STR: {
                uint64_t len = msg.mr[0];
                if (len > 24) len = 24;
                uint64_t packed[3] = { msg.mr[1], msg.mr[2], msg.mr[3] };
                const char *chars = (const char *)packed;
                for (uint64_t i = 0; i < len; i++) {
                    el0_uart_hw_putc(chars[i]);
                }
                tx_bytes += len;
                r0 = len;
                break;
            }
            case UART_IPC_GETC: {
                int ch = el0_uart_console_getc();
                if (ch >= 0) rx_bytes++;
                r0 = (uint64_t)(int64_t)ch;
                break;
            }
            case UART_IPC_GET_STATS: {
                r0 = tx_bytes;
                r1 = rx_bytes;
                r2 = reqs;
                break;
            }
            default:
                reply_tag = (uint64_t)EINVAL;
                break;
        }

        /* Atomic L4 Reply + Receive next request in a single syscall! */
        rc = sys_ipc_reply_recv(CAP_SLOT_UART_EP, reply_tag, r0, r1, r2, 0, &msg);
    }
}
