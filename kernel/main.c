#include "types.h"
#include "arch.h"
#include "uart.h"
#include "kprintf.h"
#include "pmm.h"
#include "mmu.h"
#include "gic.h"
#include "timer.h"
#include "cap.h"
#include "ipc.h"
#include "sched.h"
#include "user.h"

extern char __kernel_start[];
extern char __kernel_end[];
extern char __text_start[];
extern char __rodata_end[];
extern char __data_start[];

static void print_boot_banner(void) {
    kprintf("\n");
    kprintf(CLR_CYAN CLR_BOLD "  ╔══════════════════════════════════════════════════════════════════════╗\n");
    kprintf("  ║   _   _      _ _            ____  _  _                               ║\n");
    kprintf("  ║  | | | | ___| (_) ___  ___ / ___|| || |    AArch64 Microkernel       ║\n");
    kprintf("  ║  | |_| |/ _ \\ | |/ _ \\/ __| '_ \\| || |_   L4-Style Fastpath IPC      ║\n");
    kprintf("  ║  |  _  |  __/ | | (_) \\__ \\ (_) |__   _|  Capability + MMU Isolation ║\n");
    kprintf("  ║  |_| |_|\\___|_|_|\\___/|___/\\___/   |_|    v1.0 (ARMv8-A / EL1+EL0)   ║\n");
    kprintf("  ╚══════════════════════════════════════════════════════════════════════╝" CLR_RESET "\n\n");
}

void kernel_main(void) {
    /* 1. Initialize Early PL011 Serial Console */
    uart_init();
    print_boot_banner();

    uint64_t el = read_current_el();
    kprintf(CLR_GREEN "[BOOT]" CLR_RESET " CPU0 online at " CLR_BOLD "EL%lu" CLR_RESET
            " | Kernel image: 0x%lx .. 0x%lx (%lu KB)\n",
            el, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end,
            ((uintptr_t)__kernel_end - (uintptr_t)__kernel_start) / 1024);

    /* 2. Initialize Physical Memory Page Frame Allocator (PMM) */
    pmm_init((uintptr_t)__kernel_end, PHYS_RAM_END);
    pmm_stats_t pstats;
    pmm_get_stats(&pstats);
    kprintf(CLR_GREEN "[PMM ]" CLR_RESET " O(1) Bitmap Frame Allocator ready: "
            "%lu pages (%lu MB free, pool 0x%lx..0x%lx)\n",
            pstats.total_pages, (pstats.free_pages * PAGE_SIZE) / (1024 * 1024),
            pstats.pool_start, pstats.pool_end);

    /* 3. Initialize ARMv8-A MMU with 3-Level 4KB Page Tables & W^X Protection */
    mmu_init_kernel();
    kprintf(CLR_GREEN "[MMU ]" CLR_RESET " 39-bit 3-Level Page Tables enabled (SCTLR_EL1 M+C+I=1, 4KB granule, ASID tagged)\n");
    kprintf("       ├── RX Code/ROData : 0x%lx .. 0x%lx (EL0+EL1 Read/Exec, Write-Protected)\n",
            (uintptr_t)__text_start, (uintptr_t)__rodata_end);
    kprintf("       └── Kernel Data/RAM: 0x%lx .. 0x%lx (EL1-Only RW, EL0 No-Access)\n",
            (uintptr_t)__data_start, (uintptr_t)PHYS_RAM_END);

    /* 4. Initialize GICv2 Interrupt Controller & UART0 RX Interrupt */
    gic_init();
    gic_enable_irq(IRQ_UART0, 0x90);
    kprintf(CLR_GREEN "[GIC ]" CLR_RESET " ARM GICv2 Distributor (0x%lx) & CPU Interface (0x%lx) active\n",
            (uintptr_t)VIRT_GICD_BASE, (uintptr_t)VIRT_GICC_BASE);

    /* 5. Initialize Generic Physical Timer (100 Hz Preemption Tick) */
    timer_init(TIMER_HZ);
    kprintf(CLR_GREEN "[TIME]" CLR_RESET " ARMv8 Physical Timer @ %lu.%02lu MHz | Preemption tick: %lu Hz (PPI IRQ %u)\n",
            timer_get_freq() / 1000000ULL, (timer_get_freq() % 1000000ULL) / 10000ULL,
            (uint64_t)TIMER_HZ, IRQ_TIMER_PHYS);

    /* 6. Initialize Synchronous IPC Subsystem & Well-Known Endpoints */
    ipc_init();
    ipc_endpoint_create(EP_ID_UART,   "ep:uart_console");
    ipc_endpoint_create(EP_ID_SYSMON, "ep:sysmon_srv");
    kprintf(CLR_GREEN "[IPC ]" CLR_RESET " L4 Register-Based Fastpath IPC initialized (%u endpoints configured)\n", 2);

    /* 7. Initialize Preemptive Scheduler & Spawn Isolated EL0 User-Space Services */
    sched_init();

    /*
     * Spawn PID 1: User-Space Serial Driver Server (EL0, ASID 1, Priority 1)
     * Granted exclusive MMIO Device Capability + EL0 MMIO page mapping for UART0 (0x09000000)
     * and RECV authority on EP_ID_UART.
     */
    task_t *t_uart = task_create_user("uart_srv", user_uart_server_main, 1);
    mmu_map_page(&t_uart->vspace, VIRT_UART0_BASE, VIRT_UART0_BASE, VM_USER_DEVICE);
    cap_grant(t_uart, CAP_SLOT_UART_EP,  CAP_ENDPOINT, CAP_RIGHT_RECV | CAP_RIGHT_SEND, 0x0100, EP_ID_UART, 0);
    cap_grant(t_uart, CAP_SLOT_DEVICE_0, CAP_DEVICE,   CAP_RIGHT_READ | CAP_RIGHT_WRITE, 0x0101, 1, VIRT_UART0_BASE);

    /*
     * Spawn PID 2: User-Space System Monitor & Benchmark Server (EL0, ASID 2, Priority 2)
     * Granted RECV authority on EP_ID_SYSMON and SEND authority on EP_ID_UART.
     */
    task_t *t_sysmon = task_create_user("sysmon_srv", user_sysmon_server_main, 2);
    cap_grant(t_sysmon, CAP_SLOT_UART_EP,   CAP_ENDPOINT, CAP_RIGHT_SEND, 0x0200, EP_ID_UART, 0);
    cap_grant(t_sysmon, CAP_SLOT_SYSMON_EP, CAP_ENDPOINT, CAP_RIGHT_RECV | CAP_RIGHT_SEND, 0x0201, EP_ID_SYSMON, 0);

    /*
     * Spawn PID 3: User-Space Init / Interactive Shell & Benchmark Client (EL0, ASID 3, Priority 3)
     * Granted SEND authority to both EP_ID_UART and EP_ID_SYSMON.
     */
    task_t *t_shell = task_create_user("init_shell", user_shell_main, 3);
    cap_grant(t_shell, CAP_SLOT_UART_EP,   CAP_ENDPOINT, CAP_RIGHT_SEND, 0x0300, EP_ID_UART, 0);
    cap_grant(t_shell, CAP_SLOT_SYSMON_EP, CAP_ENDPOINT, CAP_RIGHT_SEND, 0x0301, EP_ID_SYSMON, 0);

    kprintf(CLR_GREEN "[TASK]" CLR_RESET " Spawned 3 isolated EL0 user-space tasks:\n");
    kprintf("       ├── PID %u [%-10s] EL0 ASID=%u Prio=%u | Caps: EP_UART(RECV), DEV_UART0(MMIO 0x%lx)\n",
            t_uart->id, t_uart->name, t_uart->vspace.asid, t_uart->priority, (uintptr_t)VIRT_UART0_BASE);
    kprintf("       ├── PID %u [%-10s] EL0 ASID=%u Prio=%u | Caps: EP_SYSMON(RECV), EP_UART(SEND)\n",
            t_sysmon->id, t_sysmon->name, t_sysmon->vspace.asid, t_sysmon->priority);
    kprintf("       └── PID %u [%-10s] EL0 ASID=%u Prio=%u | Caps: EP_UART(SEND), EP_SYSMON(SEND)\n\n",
            t_shell->id, t_shell->name, t_shell->vspace.asid, t_shell->priority);

    /* 8. Enable IRQs and yield from Kernel Idle Thread into EL0 User Space! */
    irq_enable();
    sched_yield();

    /* Kernel Idle Loop (PID 0, Priority 7) */
    for (;;) {
        wfi();
        sched_yield();
    }
}
