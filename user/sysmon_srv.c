#include "user.h"
#include "arch.h"

void user_sysmon_server_main(void) {
    uint64_t requests_served = 0;
    uint64_t ping_count      = 0;

    /* Announce startup via synchronous IPC to the EL0 UART Console Server */
    u_printf("\033[32m[SYSMON]\033[0m EL0 Telemetry & Benchmark Server online (PID 2, ASID 2)\n");

    ipc_msg_t msg;
    int rc = sys_ipc_recv(CAP_SLOT_SYSMON_EP, &msg);

    for (;;) {
        if (rc != EOK) {
            rc = sys_ipc_recv(CAP_SLOT_SYSMON_EP, &msg);
            continue;
        }

        requests_served++;
        uint64_t r0 = 0, r1 = 0, r2 = 0, r3 = 0;

        switch (msg.tag) {
            case SYSMON_IPC_PING:
                /* Ultra-fast register echo for L4 IPC latency benchmarking */
                ping_count++;
                r0 = msg.mr[0] ^ 0xA5A5A5A55A5A5A5AULL;
                r1 = requests_served;
                r2 = msg.badge;
                break;

            case SYSMON_IPC_QUERY:
                r0 = requests_served;
                r1 = ping_count;
                r2 = read_cntpct();
                r3 = msg.badge;
                break;

            case SYSMON_IPC_COMPUTE: {
                /* Compute FNV-1a 64-bit hash over mr0..mr3 in isolated EL0 address space */
                uint64_t hash = 0xcbf29ce484222325ULL;
                for (int i = 0; i < 4; i++) {
                    hash ^= msg.mr[i];
                    hash *= 0x100000001b3ULL;
                }
                r0 = hash;
                r1 = requests_served;
                break;
            }

            default:
                break;
        }

        /* Atomic L4 Reply + Receive next request */
        rc = sys_ipc_reply_recv(CAP_SLOT_SYSMON_EP, EOK, r0, r1, r2, r3, &msg);
    }
}
