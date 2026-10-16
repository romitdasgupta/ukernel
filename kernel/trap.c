#include "arch.h"
#include "syscall.h"
#include "sched.h"
#include "ipc.h"
#include "cap.h"
#include "pmm.h"
#include "mmu.h"
#include "gic.h"
#include "timer.h"
#include "uart.h"
#include "kprintf.h"

/*
 * Validate that a user-supplied buffer [uaddr, uaddr + size) resides inside
 * the current task's EL0-writable virtual memory (e.g. its user stack).
 */
static bool validate_user_ptr(task_t *t, uintptr_t uaddr, size_t size) {
    if (!t || !t->is_user || size == 0) return false;
    uintptr_t start = ALIGN_DOWN(uaddr, PAGE_SIZE);
    uintptr_t end   = ALIGN_UP(uaddr + size, PAGE_SIZE);

    for (uintptr_t va = start; va < end; va += PAGE_SIZE) {
        uint64_t pte = 0;
        uintptr_t pa = mmu_virt_to_phys(&t->vspace, va, &pte);
        if (!pa || !(pte & PTE_AP_EL0_RW)) {
            return false;
        }
    }
    return true;
}

/* ============================================================================
 * Microkernel System Call Dispatcher (EL0 -> EL1 via 'svc #0')
 * Syscall number in x8; arguments in x0..x5; return value in x0.
 * ============================================================================ */
void syscall_dispatch(trap_frame_t *tf) {
    task_t *cur = sched_current();
    cur->tf = tf;
    uint64_t sys_num = tf->x[8];

    switch (sys_num) {
        case SYS_YIELD:
            tf->x[0] = EOK;
            sched_yield();
            break;

        case SYS_IPC_CALL:
            sys_ipc_call_handler(tf, (uint32_t)tf->x[0]);
            break;

        case SYS_IPC_RECV:
            sys_ipc_recv_handler(tf, (uint32_t)tf->x[0]);
            break;

        case SYS_IPC_REPLY_RECV:
            sys_ipc_reply_recv_handler(tf, (uint32_t)tf->x[0]);
            break;

        case SYS_IPC_SEND:
            sys_ipc_send_handler(tf, (uint32_t)tf->x[0]);
            break;

        case SYS_SLEEP_MS: {
            uint64_t ms = tf->x[0];
            uint64_t ticks = (ms * TIMER_HZ + 999) / 1000;
            tf->x[0] = EOK;
            sched_sleep_ticks(ticks);
            break;
        }

        case SYS_GET_TASK_INFO: {
            uint32_t tid = (uint32_t)tf->x[0];
            uintptr_t uptr = (uintptr_t)tf->x[1];
            if (!validate_user_ptr(cur, uptr, sizeof(task_info_t))) {
                tf->x[0] = (uint64_t)EFAULT;
                break;
            }
            task_t *t = sched_get_task(tid);
            if (!t) {
                tf->x[0] = (uint64_t)ENOENT;
                break;
            }
            task_info_t *info = (task_info_t *)uptr;
            info->id           = t->id;
            strncpy(info->name, t->name, sizeof(info->name) - 1);
            info->state        = (uint8_t)t->state;
            info->is_user      = t->is_user;
            info->priority     = t->priority;
            info->asid         = t->vspace.asid;
            info->mapped_pages = t->vspace.mapped_pages;
            info->cpu_ticks    = t->cpu_ticks;
            info->ctx_switches = t->ctx_switches;
            info->ipc_sent     = t->ipc_sent;
            info->ipc_rcvd     = t->ipc_rcvd;
            tf->x[0] = EOK;
            break;
        }

        case SYS_GET_SYS_INFO: {
            uintptr_t uptr = (uintptr_t)tf->x[0];
            if (!validate_user_ptr(cur, uptr, sizeof(sys_info_t))) {
                tf->x[0] = (uint64_t)EFAULT;
                break;
            }
            pmm_stats_t pstats;
            pmm_get_stats(&pstats);

            uint64_t total_ipc = 0;
            uint64_t fast_ipc  = 0;
            for (uint32_t id = 1; id < MAX_ENDPOINTS; id++) {
                ipc_endpoint_t *ep = ipc_get_endpoint(id);
                if (ep) {
                    total_ipc += ep->total_msgs;
                    fast_ipc  += ep->fastpath_hits;
                }
            }

            uint32_t active_tasks = 0;
            for (uint32_t i = 0; i < MAX_TASKS; i++) {
                if (sched_get_task(i)) active_tasks++;
            }

            sys_info_t *sinfo = (sys_info_t *)uptr;
            sinfo->total_ram_bytes   = PHYS_RAM_SIZE;
            sinfo->free_ram_bytes    = pstats.free_pages * PAGE_SIZE;
            sinfo->used_pages        = pstats.used_pages;
            sinfo->free_pages        = pstats.free_pages;
            sinfo->timer_freq_hz     = timer_get_freq();
            sinfo->uptime_ms         = timer_get_uptime_ms();
            sinfo->timer_ticks       = timer_get_ticks();
            sinfo->total_ipc_msgs    = total_ipc;
            sinfo->fastpath_ipc_hits = fast_ipc;
            sinfo->active_tasks      = active_tasks;
            tf->x[0] = EOK;
            break;
        }

        case SYS_CAP_INSPECT: {
            uint32_t slot = (uint32_t)tf->x[0];
            uintptr_t uptr = (uintptr_t)tf->x[1];
            if (slot >= MAX_CAPS_PER_TASK || !validate_user_ptr(cur, uptr, sizeof(cap_info_t))) {
                tf->x[0] = (uint64_t)EINVAL;
                break;
            }
            cap_info_t *cinfo = (cap_info_t *)uptr;
            cinfo->type      = cur->cspace[slot].type;
            cinfo->rights    = cur->cspace[slot].rights;
            cinfo->badge     = cur->cspace[slot].badge;
            cinfo->obj_id    = cur->cspace[slot].obj_id;
            cinfo->phys_base = cur->cspace[slot].phys_base;
            tf->x[0] = EOK;
            break;
        }

        case SYS_FAULT_GUARD: {
            uintptr_t recovery_pc = (uintptr_t)tf->x[0];
            cur->fault_recovery_enabled = (recovery_pc != 0);
            cur->fault_recovery_pc      = recovery_pc;
            tf->x[0] = EOK;
            break;
        }

        case SYS_SHUTDOWN:
            kprintf("\n" CLR_CYAN "[KERNEL] Clean shutdown requested by task '%s' (PID %u) via PSCI." CLR_RESET "\n",
                    cur->name, cur->id);
            psci_system_off();
            break;

        case SYS_DEBUG_CONSOLE: {
            /* Require CAP_DEVICE on UART0 (granted only to uart_srv!) */
            const capability_t *dev_cap = cap_lookup(cur, CAP_SLOT_DEVICE_0, CAP_DEVICE, CAP_RIGHT_READ);
            if (!dev_cap || dev_cap->phys_base != VIRT_UART0_BASE) {
                tf->x[0] = (uint64_t)EPERM;
                break;
            }
            uint64_t op = tf->x[0];
            if (op == CONSOLE_OP_PUTC) {
                uart_putc((char)tf->x[1]);
                tf->x[0] = EOK;
            } else if (op == CONSOLE_OP_GETC) {
                tf->x[0] = (uint64_t)(int64_t)uart_rx_pop();
            } else {
                tf->x[0] = (uint64_t)EINVAL;
            }
            break;
        }

        default:
            kprintf(CLR_YELLOW "[KERNEL] Unknown syscall %lu from task '%s' (PID %u)" CLR_RESET "\n",
                    sys_num, cur->name, cur->id);
            tf->x[0] = (uint64_t)EINVAL;
            break;
    }
}

/* ============================================================================
 * Synchronous Exception from EL0 (User Space)
 * ============================================================================ */
void trap_el0_sync(trap_frame_t *tf) {
    uint64_t esr = read_esr_el1();
    uint32_t ec  = (uint32_t)((esr >> ESR_EC_SHIFT) & ESR_EC_MASK);

    if (ec == ESR_EC_SVC64) {
        syscall_dispatch(tf);
        return;
    }

    /* Hardware MMU Page / Permission Fault or Illegal Instruction in EL0 */
    uint64_t far = read_far_el1();
    uint32_t iss = (uint32_t)(esr & ESR_ISS_MASK);
    task_t *cur  = sched_current();

    kprintf(CLR_YELLOW "  [MMU FAULT GUARD] Caught EL0 Exception in task '%s' (PID %u, ASID %u):" CLR_RESET "\n",
            cur->name, cur->id, cur->vspace.asid);
    kprintf("    EC=0x%02x (%s)  ISS=0x%06x  FAR=0x%016lx  ELR=0x%016lx\n",
            ec,
            (ec == ESR_EC_DABT_EL0) ? "Data Abort from EL0" :
            (ec == ESR_EC_IABT_EL0) ? "Instruction Abort from EL0" : "Sync Exception",
            iss, far, tf->elr_el1);

    /* If task registered a fault recovery guard (e.g., for MMU isolation verification), resume cleanly */
    if (cur->fault_recovery_enabled && cur->fault_recovery_pc != 0) {
        kprintf(CLR_GREEN "    -> Kernel isolated the fault! Redirecting task ELR to recovery handler (0x%lx)." CLR_RESET "\n",
                cur->fault_recovery_pc);
        tf->elr_el1 = cur->fault_recovery_pc;
        tf->x[0]    = far; /* Pass faulting address in x0 to recovery label */
        cur->fault_recovery_enabled = false;
        cur->fault_recovery_pc      = 0;
        return;
    }

    /* Otherwise terminate the faulting user task without crashing the microkernel */
    kprintf(CLR_RED "    -> Terminating offending user task '%s' (PID %u). Kernel remains healthy." CLR_RESET "\n",
            cur->name, cur->id);
    cur->state = TASK_FAULTED;
    sched_yield();
}

/* ============================================================================
 * Synchronous Exception from EL1 (Kernel Mode)
 * ============================================================================ */
void trap_el1_sync(trap_frame_t *tf) {
    uint64_t esr = read_esr_el1();
    uint64_t far = read_far_el1();
    uint32_t ec  = (uint32_t)((esr >> ESR_EC_SHIFT) & ESR_EC_MASK);
    kpanic("Unhandled EL1 Sync Exception: EC=0x%02x ESR=0x%lx FAR=0x%lx ELR=0x%lx",
           ec, esr, far, tf->elr_el1);
}

/* ============================================================================
 * Hardware IRQ Handler (GICv2 Dispatch)
 * ============================================================================ */
void trap_irq_handler(trap_frame_t *tf) {
    task_t *cur = sched_current();
    if (cur) {
        cur->tf = tf;
    }

    uint32_t iar = gic_ack_irq();
    uint32_t irq_id = iar & 0x3FFU;

    if (irq_id == GIC_SPURIOUS_IRQ) {
        return;
    }

    if (irq_id == IRQ_TIMER_PHYS) {
        timer_handle_irq();
        gic_eoi_irq(iar);
        /* Preempt current task if its time slice expired */
        if (cur && cur->time_slice == 0) {
            sched_yield();
        }
        return;
    }

    if (irq_id == IRQ_UART0) {
        uart_handle_irq();
        gic_eoi_irq(iar);
        return;
    }

    gic_eoi_irq(iar);
}

void trap_unhandled(trap_frame_t *tf) {
    kpanic("Unhandled vector exception at ELR=0x%lx SPSR=0x%lx ESR=0x%lx",
           tf->elr_el1, tf->spsr_el1, read_esr_el1());
}
