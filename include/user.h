#ifndef UKERNEL_USER_H
#define UKERNEL_USER_H

#include "types.h"
#include "syscall.h"
#include "cap.h"
#include "ipc.h"

/* ============================================================================
 * User-Space (EL0) IPC Protocol Tags for Microkernel Services
 * ============================================================================ */

/* UART Console Server (EP_ID_UART, CAP_SLOT_UART_EP) Protocol */
#define UART_IPC_PUTC           1   /* mr0 = char */
#define UART_IPC_WRITE_STR      2   /* mr0 = ptr/packed chars or len, mr1..mr3 = packed 24 bytes */
#define UART_IPC_GETC           3   /* returns mr0 = char (or -1 if none) */
#define UART_IPC_GET_STATS      4   /* returns mr0 = tx_bytes, mr1 = rx_bytes, mr2 = ipc_reqs */

/* System Monitor & Benchmark Server (EP_ID_SYSMON, CAP_SLOT_SYSMON_EP) Protocol */
#define SYSMON_IPC_PING         10  /* Fastpath no-op echo: returns mr0 = arg0 ^ 0xA5A5A5A5 */
#define SYSMON_IPC_QUERY        11  /* Returns server health, uptime, and request counter */
#define SYSMON_IPC_COMPUTE      12  /* Performs checksum/hash calculation in isolated EL0 server */

/* ============================================================================
 * User-Space System Call Wrappers (EL0 -> EL1 via 'svc #0')
 * ============================================================================ */
void sys_yield(void);
void sys_sleep_ms(uint64_t ms);
int  sys_ipc_call(uint32_t cap_slot, uint64_t tag,
                  uint64_t mr0, uint64_t mr1, uint64_t mr2, uint64_t mr3,
                  ipc_msg_t *reply);
int  sys_ipc_recv(uint32_t cap_slot, ipc_msg_t *msg);
int  sys_ipc_reply_recv(uint32_t cap_slot, uint64_t reply_tag,
                        uint64_t mr0, uint64_t mr1, uint64_t mr2, uint64_t mr3,
                        ipc_msg_t *next_msg);
int  sys_get_task_info(uint32_t task_id, task_info_t *out);
int  sys_get_sys_info(sys_info_t *out);
int  sys_cap_inspect(uint32_t slot, cap_info_t *out);
int  sys_fault_guard(uintptr_t recovery_pc);
void sys_shutdown(void) NORETURN;

/* Client helper functions for talking to the EL0 UART Server over IPC */
void u_putc(char c);
void u_puts(const char *s);
int  u_getc(void);
void u_printf(const char *fmt, ...);

/* Entry points for our 3 EL0 user-space tasks */
void user_uart_server_main(void);
void user_sysmon_server_main(void);
void user_shell_main(void);

#endif /* UKERNEL_USER_H */
