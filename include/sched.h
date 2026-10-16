#ifndef UKERNEL_SCHED_H
#define UKERNEL_SCHED_H

#include "types.h"
#include "arch.h"
#include "mmu.h"
#include "cap.h"
#include "ipc.h"

#define MAX_TASKS           16
#define TASK_NAME_MAX       20
#define KERNEL_STACK_PAGES  2                   /* 8 KB kernel stack per task */
#define KERNEL_STACK_SIZE   (KERNEL_STACK_PAGES * PAGE_SIZE)
#define USER_STACK_PAGES    2                   /* 8 KB user stack per EL0 task */
#define USER_STACK_SIZE     (USER_STACK_PAGES * PAGE_SIZE)

/* Virtual address where each EL0 task's private user stack is mapped */
#define USER_STACK_VADDR_BASE  0x0000000070000000ULL
#define USER_STACK_VADDR_TOP   (USER_STACK_VADDR_BASE + USER_STACK_SIZE)

typedef enum task_state {
    TASK_UNUSED       = 0,
    TASK_READY        = 1,
    TASK_RUNNING      = 2,
    TASK_BLOCKED_RECV = 3,  /* Server waiting for incoming IPC message */
    TASK_BLOCKED_SEND = 4,  /* Client waiting for server to enter RECV */
    TASK_BLOCKED_REPL = 5,  /* Client waiting for server's synchronous REPLY */
    TASK_SLEEPING     = 6,  /* Sleeping until wakeup_tick */
    TASK_FAULTED      = 7,  /* Terminated due to unhandled EL0 fault */
    TASK_EXITED       = 8,
} task_state_t;

typedef struct task {
    uint32_t        id;                         /* PID / Task ID */
    char            name[TASK_NAME_MAX];        /* Human-readable task name */
    task_state_t    state;                      /* Scheduling state */
    bool            is_user;                    /* true = EL0 task, false = EL1 kernel thread */
    uint8_t         priority;                   /* Priority (0 = highest, 7 = idle) */
    uint32_t        time_slice;                 /* Remaining ticks in current quantum */

    cpu_context_t   context;                    /* Callee-saved register context for cpu_switch_to */
    trap_frame_t   *tf;                         /* Pointer to trap_frame at top of kernel stack */
    uintptr_t       kstack_base;                /* Physical base of kernel stack */
    uintptr_t       kstack_top;                 /* Top of kernel stack */
    uintptr_t       ustack_phys;                /* Physical base of private user stack */
    uintptr_t       ustack_top_va;              /* Virtual top of user stack (SP_EL0) */

    vspace_t        vspace;                     /* Hardware MMU Virtual Address Space & ASID */
    capability_t    cspace[MAX_CAPS_PER_TASK];  /* Capability table (CSpace) */

    /* IPC state */
    ipc_msg_t       ipc_msg;                    /* Staging / in-flight message buffer */
    struct task    *caller;                     /* Client task waiting for reply from this server */
    struct task    *ipc_next;                   /* Intrusive pointer for endpoint send FIFO */
    uint32_t        blocked_ep_id;              /* Endpoint ID currently blocking this task */

    /* Telemetry & Diagnostics */
    uint64_t        cpu_ticks;                  /* Timer ticks accrued while running */
    uint64_t        ctx_switches;               /* Number of times scheduled in */
    uint64_t        ipc_sent;                   /* Number of IPC messages sent */
    uint64_t        ipc_rcvd;                   /* Number of IPC messages received */
    uint64_t        wakeup_tick;                /* Tick to wake up if TASK_SLEEPING */
    bool            fault_recovery_enabled;     /* For MMU isolation self-test */
    uintptr_t       fault_recovery_pc;          /* Recovery PC when testing EL0 faults */
} task_t;

/* Assembly context switch & EL0 entry point */
extern void cpu_switch_to(cpu_context_t *prev, cpu_context_t *next);
extern void ret_from_fork(void);

void        sched_init(void);
task_t     *task_create_kernel(const char *name, void (*entry)(void), uint8_t priority);
task_t     *task_create_user(const char *name, void (*entry)(void), uint8_t priority);
task_t     *sched_current(void);
task_t     *sched_get_task(uint32_t id);
void        sched_yield(void);
void        sched_tick(void);
void        sched_sleep_ticks(uint64_t ticks);
void        sched_direct_switch(task_t *target);
const char *task_state_name(task_state_t st);

#endif /* UKERNEL_SCHED_H */
