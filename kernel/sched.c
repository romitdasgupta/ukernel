#include "sched.h"
#include "pmm.h"
#include "mmu.h"
#include "timer.h"
#include "kprintf.h"

#define DEFAULT_TIME_SLICE  5   /* 5 ticks = 50ms preemption quantum */

static task_t   g_tasks[MAX_TASKS];
static task_t  *g_current_task = NULL;
static uint32_t g_last_sched_idx = 0;

extern char __boot_stack_bottom[];
extern char __boot_stack_top[];

const char *task_state_name(task_state_t st) {
    switch (st) {
        case TASK_UNUSED:       return "UNUSED";
        case TASK_READY:        return "READY";
        case TASK_RUNNING:      return "RUNNING";
        case TASK_BLOCKED_RECV: return "WAIT_RECV";
        case TASK_BLOCKED_SEND: return "WAIT_SEND";
        case TASK_BLOCKED_REPL: return "WAIT_REPL";
        case TASK_SLEEPING:     return "SLEEPING";
        case TASK_FAULTED:      return "FAULTED";
        case TASK_EXITED:       return "EXITED";
        default:                return "UNKNOWN";
    }
}

void sched_init(void) {
    memset(g_tasks, 0, sizeof(g_tasks));

    /* Initialize Task 0 as the Kernel Idle Thread (running on boot stack) */
    task_t *idle = &g_tasks[0];
    idle->id          = 0;
    strncpy(idle->name, "idle_el1", TASK_NAME_MAX - 1);
    idle->state       = TASK_RUNNING;
    idle->is_user     = false;
    idle->priority    = 7; /* Lowest priority */
    idle->time_slice  = DEFAULT_TIME_SLICE;
    idle->kstack_base = (uintptr_t)__boot_stack_bottom;
    idle->kstack_top  = (uintptr_t)__boot_stack_top;
    idle->vspace      = *mmu_get_kernel_vspace();
    cap_init_task(idle);

    g_current_task = idle;
}

task_t *sched_current(void) {
    return g_current_task;
}

task_t *sched_get_task(uint32_t id) {
    if (id >= MAX_TASKS || g_tasks[id].state == TASK_UNUSED) {
        return NULL;
    }
    return &g_tasks[id];
}

static task_t *alloc_task_slot(void) {
    for (uint32_t i = 1; i < MAX_TASKS; i++) {
        if (g_tasks[i].state == TASK_UNUSED) {
            memset(&g_tasks[i], 0, sizeof(task_t));
            g_tasks[i].id = i;
            return &g_tasks[i];
        }
    }
    return NULL;
}

task_t *task_create_kernel(const char *name, void (*entry)(void), uint8_t priority) {
    task_t *t = alloc_task_slot();
    if (!t) return NULL;

    uintptr_t kstack = pmm_alloc_pages(KERNEL_STACK_PAGES);
    if (!kstack) return NULL;

    strncpy(t->name, name, TASK_NAME_MAX - 1);
    t->is_user     = false;
    t->priority    = priority;
    t->time_slice  = DEFAULT_TIME_SLICE;
    t->kstack_base = kstack;
    t->kstack_top  = kstack + KERNEL_STACK_SIZE;
    t->vspace      = *mmu_get_kernel_vspace();
    cap_init_task(t);

    /* Set up cpu_context so cpu_switch_to jumps to ret_from_fork with x19 = entry */
    t->context.x19 = (uint64_t)entry;
    t->context.x30 = (uint64_t)ret_from_fork;
    t->context.sp  = t->kstack_top;
    t->state       = TASK_READY;

    return t;
}

task_t *task_create_user(const char *name, void (*entry)(void), uint8_t priority) {
    task_t *t = alloc_task_slot();
    if (!t) return NULL;

    /* Allocate 8KB kernel stack and 8KB private physical user stack */
    uintptr_t kstack = pmm_alloc_pages(KERNEL_STACK_PAGES);
    uintptr_t ustack = pmm_alloc_pages(USER_STACK_PAGES);
    if (!kstack || !ustack) {
        return NULL;
    }

    strncpy(t->name, name, TASK_NAME_MAX - 1);
    t->is_user       = true;
    t->priority      = priority;
    t->time_slice    = DEFAULT_TIME_SLICE;
    t->kstack_base   = kstack;
    t->kstack_top    = kstack + KERNEL_STACK_SIZE;
    t->ustack_phys   = ustack;
    t->ustack_top_va = USER_STACK_VADDR_TOP;
    cap_init_task(t);

    /* Create isolated MMU Virtual Address Space with unique hardware ASID */
    if (mmu_create_vspace(&t->vspace, (uint16_t)t->id) != EOK) {
        return NULL;
    }

    /* Map private user stack at virtual address USER_STACK_VADDR_BASE (EL0 RW, UXN) */
    if (mmu_map_range(&t->vspace, USER_STACK_VADDR_BASE, ustack, USER_STACK_SIZE, VM_USER_RW) != EOK) {
        return NULL;
    }
    t->vspace.mapped_pages += USER_STACK_PAGES;

    /* Grant self-inspection capability in slot 0 */
    cap_grant(t, CAP_SLOT_SELF_TASK, CAP_TASK, CAP_RIGHT_READ, (uint16_t)t->id, t->id, 0);

    /* Prepare initial trap_frame_t at the top of the task's kernel stack for ERET into EL0 */
    trap_frame_t *tf = (trap_frame_t *)(t->kstack_top - sizeof(trap_frame_t));
    memset(tf, 0, sizeof(trap_frame_t));
    tf->sp_el0   = t->ustack_top_va;
    tf->elr_el1  = (uint64_t)entry;
    tf->spsr_el1 = SPSR_EL0t; /* Unprivileged EL0 using SP_EL0, IRQs enabled! */

    t->tf          = tf;
    t->context.x19 = 0; /* x19 == 0 tells ret_from_fork to RESTORE_TRAP_FRAME & ERET to EL0 */
    t->context.x30 = (uint64_t)ret_from_fork;
    t->context.sp  = (uintptr_t)tf;
    t->state       = TASK_READY;

    return t;
}

/*
 * Direct Process Switch (used by L4 IPC fastpath):
 * Immediately switches from g_current_task to target without scanning runqueues.
 */
void sched_direct_switch(task_t *target) {
    if (!target || target == g_current_task) {
        return;
    }

    task_t *prev = g_current_task;
    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
    }

    target->state      = TASK_RUNNING;
    target->time_slice = DEFAULT_TIME_SLICE;
    target->ctx_switches++;
    g_current_task     = target;

    /* Switch hardware MMU page table & ASID (TTBR0_EL1) */
    mmu_switch_vspace(&target->vspace);

    /* Context switch kernel stack & callee-saved registers */
    cpu_switch_to(&prev->context, &target->context);
}

/*
 * Priority-aware Round-Robin Scheduler
 */
void sched_yield(void) {
    uint64_t flags = irq_save();
    uint64_t now_tick = timer_get_ticks();

    /* Wake up any sleeping tasks whose deadline has passed */
    for (uint32_t i = 1; i < MAX_TASKS; i++) {
        if (g_tasks[i].state == TASK_SLEEPING && now_tick >= g_tasks[i].wakeup_tick) {
            g_tasks[i].state = TASK_READY;
        }
    }

    task_t *prev = g_current_task;
    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
    }

    /* Select highest-priority (lowest numeric priority value) READY task in round-robin order */
    task_t *best = NULL;
    uint8_t best_prio = 0xFF;

    for (uint32_t step = 1; step <= MAX_TASKS; step++) {
        uint32_t idx = (g_last_sched_idx + step) % MAX_TASKS;
        if (idx == 0) continue; /* Check idle task last */
        task_t *cand = &g_tasks[idx];
        if (cand->state == TASK_READY && cand->priority < best_prio) {
            best = cand;
            best_prio = cand->priority;
        }
    }

    if (!best) {
        best = &g_tasks[0]; /* Fall back to idle task */
    } else {
        g_last_sched_idx = best->id;
    }

    best->state      = TASK_RUNNING;
    best->time_slice = DEFAULT_TIME_SLICE;
    if (best != prev) {
        best->ctx_switches++;
        g_current_task = best;
        mmu_switch_vspace(&best->vspace);
        cpu_switch_to(&prev->context, &best->context);
    }

    irq_restore(flags);
}

void sched_tick(void) {
    uint64_t now_tick = timer_get_ticks();

    for (uint32_t i = 1; i < MAX_TASKS; i++) {
        if (g_tasks[i].state == TASK_SLEEPING && now_tick >= g_tasks[i].wakeup_tick) {
            g_tasks[i].state = TASK_READY;
        }
    }

    if (g_current_task) {
        g_current_task->cpu_ticks++;
        if (g_current_task->time_slice > 0) {
            g_current_task->time_slice--;
        }
    }
}

void sched_sleep_ticks(uint64_t ticks) {
    if (ticks == 0) {
        sched_yield();
        return;
    }
    uint64_t flags = irq_save();
    task_t *cur = g_current_task;
    cur->wakeup_tick = timer_get_ticks() + ticks;
    cur->state       = TASK_SLEEPING;
    irq_restore(flags);
    sched_yield();
}
