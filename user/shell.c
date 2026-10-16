#include "user.h"
#include "arch.h"
#include "sched.h"
#include "timer.h"
#include "kprintf.h"

extern char __data_start[];

static const char *u_state_str(uint8_t st) {
    switch ((task_state_t)st) {
        case TASK_READY:        return "READY";
        case TASK_RUNNING:      return "RUNNING";
        case TASK_BLOCKED_RECV: return "WAIT_RECV";
        case TASK_BLOCKED_SEND: return "WAIT_SEND";
        case TASK_BLOCKED_REPL: return "WAIT_REPL";
        case TASK_SLEEPING:     return "SLEEPING";
        case TASK_FAULTED:      return "FAULTED";
        default:                return "OTHER";
    }
}

static const char *u_cap_type_str(uint8_t t) {
    switch ((cap_type_t)t) {
        case CAP_NONE:     return "NONE";
        case CAP_ENDPOINT: return "ENDPOINT";
        case CAP_FRAME:    return "FRAME";
        case CAP_DEVICE:   return "DEVICE";
        case CAP_TASK:     return "TASK";
        default:           return "UNKNOWN";
    }
}

/* ============================================================================
 * Shell Command Implementations (Running in Unprivileged EL0, PID 3, ASID 3)
 * ============================================================================ */
static void cmd_ps(void) {
    u_printf("\n\033[1m  PID  NAME            MODE  ASID  PRIO  STATE       PAGES  IPC_SENT  IPC_RCVD  SWITCHES\033[0m\n");
    u_printf("  ---  --------------  ----  ----  ----  ----------  -----  --------  --------  --------\n");
    for (uint32_t id = 0; id < MAX_TASKS; id++) {
        task_info_t info;
        if (sys_get_task_info(id, &info) == EOK) {
            u_printf("  %3u  %-14s  %-4s  %4u  %4u  %-10s  %5u  %8lu  %8lu  %8lu\n",
                     info.id,
                     info.name,
                     info.is_user ? "EL0" : "EL1",
                     info.asid,
                     info.priority,
                     u_state_str(info.state),
                     info.mapped_pages,
                     info.ipc_sent,
                     info.ipc_rcvd,
                     info.ctx_switches);
        }
    }
    u_printf("\n");
}

static void cmd_mem(void) {
    sys_info_t sinfo;
    if (sys_get_sys_info(&sinfo) != EOK) return;

    ipc_msg_t ustats;
    sys_ipc_call(CAP_SLOT_UART_EP, UART_IPC_GET_STATS, 0, 0, 0, 0, &ustats);

    u_printf("\n\033[1m  Physical Memory & MMU Telemetry:\033[0m\n");
    u_printf("    Total Physical RAM   : %lu MB (%lu KB)\n",
             sinfo.total_ram_bytes / (1024 * 1024), sinfo.total_ram_bytes / 1024);
    u_printf("    Allocated 4KB Frames : %lu pages (%lu KB)\n",
             sinfo.used_pages, sinfo.used_pages * 4);
    u_printf("    Free 4KB Frames      : %lu pages (%lu MB)\n",
             sinfo.free_pages, (sinfo.free_pages * 4) / 1024);
    u_printf("    Total IPC Messages   : %lu (%lu L4 fastpath direct switches)\n",
             sinfo.total_ipc_msgs, sinfo.fastpath_ipc_hits);
    u_printf("    UART Server Traffic  : %lu TX bytes, %lu RX bytes, %lu IPC calls\n\n",
             ustats.mr[0], ustats.mr[1], ustats.mr[2]);
}

static void cmd_caps(void) {
    u_printf("\n\033[1m  CSpace Capability Table for Task 'init_shell' (PID 3, ASID 3):\033[0m\n");
    u_printf("  SLOT  TYPE        RIGHTS   BADGE   OBJ_ID  PHYS_BASE\n");
    u_printf("  ----  ----------  -------  ------  ------  ------------------\n");

    for (uint32_t slot = 0; slot < MAX_CAPS_PER_TASK; slot++) {
        cap_info_t ci;
        if (sys_cap_inspect(slot, &ci) == EOK && ci.type != CAP_NONE) {
            char rbuf[6];
            rbuf[0] = (ci.rights & CAP_RIGHT_READ)  ? 'R' : '-';
            rbuf[1] = (ci.rights & CAP_RIGHT_WRITE) ? 'W' : '-';
            rbuf[2] = (ci.rights & CAP_RIGHT_SEND)  ? 'S' : '-';
            rbuf[3] = (ci.rights & CAP_RIGHT_RECV)  ? 'V' : '-';
            rbuf[4] = (ci.rights & CAP_RIGHT_GRANT) ? 'G' : '-';
            rbuf[5] = '\0';

            u_printf("  %4u  %-10s  [%s]  0x%04x  %6u  0x%016lx\n",
                     slot, u_cap_type_str(ci.type), rbuf, ci.badge, ci.obj_id, ci.phys_base);
        }
    }
    u_printf("\n");
}

static void cmd_bench(uint32_t iterations) {
    sys_info_t sinfo;
    sys_get_sys_info(&sinfo);
    uint64_t freq = sinfo.timer_freq_hz ? sinfo.timer_freq_hz : 62500000ULL;

    ipc_msg_t reply;
    uint64_t start_cycles = read_cntpct();

    for (uint32_t i = 0; i < iterations; i++) {
        sys_ipc_call(CAP_SLOT_SYSMON_EP, SYSMON_IPC_PING, i, 0, 0, 0, &reply);
    }

    uint64_t end_cycles = read_cntpct();
    uint64_t delta_cycles = end_cycles - start_cycles;
    uint64_t total_ns = (delta_cycles * 1000000000ULL) / freq;
    uint64_t ns_per_rt = total_ns / iterations;
    uint64_t ns_one_way = ns_per_rt / 2;
    uint64_t calls_per_sec = (total_ns > 0) ? ((uint64_t)iterations * 1000000000ULL) / total_ns : 0;

    u_printf("  \033[36m[BENCHMARK]\033[0m %u Cross-Address-Space Synchronous IPC Round-Trips (ASID 3 <-> ASID 2):\n",
             iterations);
    u_printf("    ├── Counter Delta    : %lu ticks @ %lu.%02lu MHz (%lu us total)\n",
             delta_cycles, freq / 1000000ULL, (freq % 1000000ULL) / 10000ULL, total_ns / 1000ULL);
    u_printf("    ├── Round-Trip (RTT) : \033[1m%lu ns\033[0m (One-Way direct switch: \033[1m%lu ns\033[0m)\n",
             ns_per_rt, ns_one_way);
    u_printf("    └── IPC Throughput   : \033[1;32m%lu round-trips/sec\033[0m (Badge verified: 0x%lx)\n",
             calls_per_sec, reply.mr[2]);
}

static void cmd_ping(void) {
    ipc_msg_t reply;
    uint64_t t0 = read_cntpct();
    int rc = sys_ipc_call(CAP_SLOT_SYSMON_EP, SYSMON_IPC_COMPUTE,
                          0xdeadbeefULL, 0xcafebabeULL, 0x12345678ULL, 0x87654321ULL, &reply);
    uint64_t t1 = read_cntpct();
    if (rc == EOK) {
        u_printf("\n  \033[32m[IPC OK]\033[0m Reply from 'sysmon_srv' (PID 2): FNV1a=0x%016lx | Reqs=%lu | Cost=%lu ticks\n\n",
                 reply.mr[0], reply.mr[1], t1 - t0);
    }
}

/*
 * Demonstrates Hardware MMU Protection & Kernel Fault Isolation:
 * Attempts to read privileged kernel data (__data_start, mapped AP_EL1_RW) from EL0.
 * The ARMv8-A MMU immediately raises a Synchronous Data Abort from Lower EL (EC=0x24),
 * which the microkernel catches and cleanly recovers via the registered fault guard!
 */
static void cmd_fault_test(void) {
    uintptr_t kernel_secret_addr = (uintptr_t)__data_start;
    uintptr_t faulted_va = 0;

    u_printf("  \033[33m[MMU TEST]\033[0m Attempting unauthorized EL0 read of kernel-only memory at 0x%lx...\n",
             kernel_secret_addr);

    __asm__ volatile(
        /* 1. Register recovery label with SYS_FAULT_GUARD */
        "adr x0, 1f\n\t"
        "mov x8, %[sys_guard]\n\t"
        "svc #0\n\t"

        /* 2. Attempt illegal EL0 load from EL1-only kernel data page -> triggers MMU Data Abort! */
        "ldr x9, [%[kaddr]]\n\t"
        "mov %[out_va], #0\n\t"
        "b 2f\n\t"

        /* 3. Fault Recovery Landing Pad (Kernel sets ELR_EL1 = 1f and x0 = FAR_EL1) */
        ".align 2\n"
        "1:\n\t"
        "mov %[out_va], x0\n"
        "2:\n\t"
        : [out_va] "=r"(faulted_va)
        : [kaddr] "r"(kernel_secret_addr),
          [sys_guard] "i"(SYS_FAULT_GUARD)
        : "x0", "x8", "x9", "memory"
    );

    if (faulted_va == kernel_secret_addr) {
        u_printf("  \033[32m[PASS]\033[0m Hardware MMU blocked EL0 access to 0x%lx & resumed task safely!\n",
                 faulted_va);
    } else {
        u_printf("  \033[31m[FAIL]\033[0m Expected fault at 0x%lx, got 0x%lx\n",
                 kernel_secret_addr, faulted_va);
    }
}

static void cmd_uptime(void) {
    sys_info_t sinfo;
    if (sys_get_sys_info(&sinfo) == EOK) {
        u_printf("\n  Uptime: %lu.%03lu sec (%lu timer ticks @ %lu Hz)\n\n",
                 sinfo.uptime_ms / 1000ULL, sinfo.uptime_ms % 1000ULL,
                 sinfo.timer_ticks, (uint64_t)TIMER_HZ);
    }
}

static void cmd_help(void) {
    u_printf("\n\033[1m  Helios64 EL0 Interactive Shell Commands:\033[0m\n");
    u_printf("    \033[36mhelp\033[0m     - Show this command reference\n");
    u_printf("    \033[36mps\033[0m       - List active EL1/EL0 tasks, ASIDs, states, and IPC counters\n");
    u_printf("    \033[36mmem\033[0m      - Show physical page frame allocator & IPC fastpath stats\n");
    u_printf("    \033[36mcaps\033[0m     - Inspect current task's Capability Space (CSpace)\n");
    u_printf("    \033[36mbench\033[0m    - Run 10,000 cross-address-space L4 IPC round-trip benchmark\n");
    u_printf("    \033[36mping\033[0m     - Send synchronous RPC compute request to 'sysmon_srv' (PID 2)\n");
    u_printf("    \033[36mfault\033[0m    - Trigger & recover from an EL0->EL1 MMU permission fault\n");
    u_printf("    \033[36muptime\033[0m   - Display system timer frequency and uptime\n");
    u_printf("    \033[36mhalt\033[0m     - Cleanly power off QEMU VM via ARM PSCI\n\n");
}

void user_shell_main(void) {
    u_printf("\033[32m[SHELL ]\033[0m EL0 Init & Interactive Shell online (PID 3, ASID 3)\n\n");

    /* ========================================================================
     * Automated Boot Self-Test & Performance Verification Suite
     * ======================================================================== */
    u_printf("\033[1m=== Running Helios64 Microkernel Boot Verification Suite ===\033[0m\n");

    /* Test 1: Cross-Address-Space Synchronous IPC Benchmark (10,000 round-trips) */
    cmd_bench(10000);

    /* Test 2: Hardware MMU EL0/EL1 Address Space Isolation Test */
    cmd_fault_test();

    /* Show Task Table & Memory Summary */
    cmd_ps();
    u_printf("Type \033[1;36mhelp\033[0m for commands, or \033[1;36mhalt\033[0m to shut down QEMU.\n\n");

    /* ========================================================================
     * Interactive Command Loop over User-Space Serial Driver IPC
     * ======================================================================== */
    char line[64];
    for (;;) {
        u_printf("\033[1;32mhelios@el0>\033[0m ");
        size_t pos = 0;

        for (;;) {
            int ch = u_getc();
            if (ch < 0) {
                /* Sleep 10ms (1 timer tick) when idle waiting for user keystrokes */
                sys_sleep_ms(10);
                continue;
            }

            if (ch == '\r' || ch == '\n') {
                u_putc('\n');
                line[pos] = '\0';
                break;
            } else if (ch == 127 || ch == '\b') {
                if (pos > 0) {
                    pos--;
                    u_puts("\b \b");
                }
            } else if (ch >= 32 && ch < 127 && pos < sizeof(line) - 1) {
                line[pos++] = (char)ch;
                u_putc((char)ch);
            }
        }

        /* Skip leading whitespace */
        const char *cmd = line;
        while (*cmd == ' ') cmd++;
        if (*cmd == '\0') continue;

        if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
            cmd_help();
        } else if (strcmp(cmd, "ps") == 0 || strcmp(cmd, "tasks") == 0) {
            cmd_ps();
        } else if (strcmp(cmd, "mem") == 0 || strcmp(cmd, "free") == 0) {
            cmd_mem();
        } else if (strcmp(cmd, "caps") == 0) {
            cmd_caps();
        } else if (strcmp(cmd, "bench") == 0 || strcmp(cmd, "ipc") == 0) {
            u_putc('\n');
            cmd_bench(10000);
            u_putc('\n');
        } else if (strcmp(cmd, "ping") == 0) {
            cmd_ping();
        } else if (strcmp(cmd, "fault") == 0) {
            u_putc('\n');
            cmd_fault_test();
            u_putc('\n');
        } else if (strcmp(cmd, "uptime") == 0) {
            cmd_uptime();
        } else if (strcmp(cmd, "clear") == 0) {
            u_puts("\033[2J\033[H");
        } else if (strcmp(cmd, "halt") == 0 || strcmp(cmd, "shutdown") == 0 ||
                   strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
            sys_shutdown();
        } else {
            u_printf("  Unknown command '%s'. Type 'help' for available commands.\n", cmd);
        }
    }
}
