#ifndef UKERNEL_SYSCALL_H
#define UKERNEL_SYSCALL_H

#include "types.h"
#include "arch.h"

/* ============================================================================
 * Microkernel System Call Numbers (invoked via 'svc #0' from EL0)
 * Minimal L4-inspired ABI: only 11 syscalls total!
 * ============================================================================ */
#define SYS_YIELD           0   /* Voluntarily yield CPU time slice */
#define SYS_IPC_CALL        1   /* Synchronous Call (Send + Wait for Reply) */
#define SYS_IPC_RECV        2   /* Block waiting to Receive on an Endpoint */
#define SYS_IPC_REPLY_RECV  3   /* Reply to last caller + atomically wait for next Receive */
#define SYS_IPC_SEND        4   /* One-way Synchronous Send */
#define SYS_SLEEP_MS        5   /* Sleep for N milliseconds */
#define SYS_GET_TASK_INFO   6   /* Query task table entry (for ps/diagnostics) */
#define SYS_GET_SYS_INFO    7   /* Query PMM, MMU, IPC, and Timer telemetry */
#define SYS_CAP_INSPECT     8   /* Inspect capability slot in current task's CSpace */
#define SYS_FAULT_GUARD     9   /* Register/clear an EL0 fault recovery handler (for MMU test) */
#define SYS_SHUTDOWN        10  /* Cleanly power off machine via PSCI */
#define SYS_DEBUG_CONSOLE   11  /* Emergency/Direct console I/O for UART driver server */

/* System-wide Telemetry Structure returned by SYS_GET_SYS_INFO */
typedef struct sys_info {
    uint64_t total_ram_bytes;
    uint64_t free_ram_bytes;
    uint64_t used_pages;
    uint64_t free_pages;
    uint64_t timer_freq_hz;
    uint64_t uptime_ms;
    uint64_t timer_ticks;
    uint64_t total_ipc_msgs;
    uint64_t fastpath_ipc_hits;
    uint32_t active_tasks;
} sys_info_t;

/* Per-Task Telemetry Structure returned by SYS_GET_TASK_INFO */
typedef struct task_info {
    uint32_t id;
    char     name[20];
    uint8_t  state;
    bool     is_user;
    uint8_t  priority;
    uint16_t asid;
    uint32_t mapped_pages;
    uint64_t cpu_ticks;
    uint64_t ctx_switches;
    uint64_t ipc_sent;
    uint64_t ipc_rcvd;
} task_info_t;

/* Capability Info Structure returned by SYS_CAP_INSPECT */
typedef struct cap_info {
    uint8_t   type;
    uint8_t   rights;
    uint16_t  badge;
    uint32_t  obj_id;
    uintptr_t phys_base;
} cap_info_t;

/* Console Driver Operations for SYS_DEBUG_CONSOLE */
#define CONSOLE_OP_PUTC     0
#define CONSOLE_OP_PUTS     1
#define CONSOLE_OP_GETC     2

void syscall_dispatch(trap_frame_t *tf);

#endif /* UKERNEL_SYSCALL_H */
