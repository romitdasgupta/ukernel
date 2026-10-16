#ifndef UKERNEL_IPC_H
#define UKERNEL_IPC_H

#include "types.h"
#include "arch.h"

#define MAX_ENDPOINTS       16

/* Well-known Endpoint IDs */
#define EP_ID_UART          1   /* Serial Console Driver Server Endpoint */
#define EP_ID_SYSMON        2   /* System Monitor / Benchmark Server Endpoint */

/* L4-Style Register-Based IPC Message Payload (transferred in CPU registers) */
typedef struct ipc_msg {
    uint64_t tag;           /* Message opcode / type tag */
    uint64_t badge;         /* Kernel-stamped unforgeable capability badge / sender PID */
    uint64_t mr[4];         /* 4 x 64-bit hardware message registers (MR0..MR3) */
} ipc_msg_t;

struct task;

/* Kernel IPC Endpoint Object */
typedef struct ipc_endpoint {
    uint32_t     id;
    bool         allocated;
    const char  *name;
    struct task *receiver;      /* Server task currently blocked in RECV on this endpoint */
    struct task *send_queue_head; /* FIFO queue of client tasks waiting to SEND */
    struct task *send_queue_tail;
    uint64_t     total_msgs;    /* Total messages delivered through this endpoint */
    uint64_t     fastpath_hits; /* Count of direct-switch L4 fastpath deliveries */
} ipc_endpoint_t;

void ipc_init(void);
int  ipc_endpoint_create(uint32_t id, const char *name);
ipc_endpoint_t *ipc_get_endpoint(uint32_t id);

/* Core Synchronous IPC Operations invoked from Syscall Handler */
void sys_ipc_call_handler(trap_frame_t *tf, uint32_t cap_slot);
void sys_ipc_recv_handler(trap_frame_t *tf, uint32_t cap_slot);
void sys_ipc_reply_recv_handler(trap_frame_t *tf, uint32_t cap_slot);
void sys_ipc_send_handler(trap_frame_t *tf, uint32_t cap_slot);

#endif /* UKERNEL_IPC_H */
