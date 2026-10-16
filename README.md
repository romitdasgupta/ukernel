# Helios64 — AArch64 L4-Style Microkernel

**Helios64** is a lightweight, high-performance 64-bit ARMv8-A (`AArch64`) microkernel written from scratch in C and ARM64 assembly. It boots on the QEMU `virt` machine (`cortex-a72`) and implements L4-style synchronous register-based IPC with direct process switching, capability-based access control (`CSpace`), hardware `ASID`-tagged 3-level MMU virtual memory isolation, preemptive scheduling, and isolated user-mode (`EL0`) driver and shell services.

> 📖 **See [ARCHITECTURE.md](ARCHITECTURE.md) for a deep-dive technical reference covering the boot sequence, 3-level MMU page tables, capability CSpace, L4 IPC fastpath, and user-space services.**

---

## Features

- **Adaptive Boot (`EL3` / `EL2` $\rightarrow$ `EL1h`)**: Cleanly transitions from `EL3` or `EL2` into `EL1h`, configures `VBAR_EL1`, enables FP/SIMD (`CPACR_EL1`), and exposes the hardware physical counter (`CNTPCT_EL0`) for cycle-accurate user-space benchmarking.
- **$O(1)$ Physical Page Frame Allocator (`kernel/pmm.c`)**: Bitmap frame allocator accelerated with hardware count-trailing-zeros (`__builtin_ctzll`) and a roving allocation hint.
- **3-Level 39-Bit MMU with Hardware `ASID` Tagging (`kernel/mmu.c`)**:
  - 4 KB page granule (`L1 -> L2 -> L3`) with `W^X` protection and strict `EL0`/`EL1` privilege separation.
  - Per-task 16-bit hardware `ASID` in `TTBR0_EL1` (`PTE_NG`) to avoid TLB flushes across context switches.
  - Hardware fault isolation (`trap_el0_sync`): catches `EL0` Data/Instruction Aborts (`ESR_EL1` `EC=0x24`/`0x20`) without crashing the kernel.
- **Capability-Based Security (`kernel/cap.c`)**: Per-task Capability Space (`CSpace`) enforcing fine-grained rights (`READ`, `WRITE`, `SEND`, `RECV`, `GRANT`) and kernel-stamped unforgeable sender badges on endpoints, MMIO devices, frames, and tasks.
- **L4-Style Fastpath Synchronous IPC (`kernel/ipc.c`)**:
  - Zero-copy register message passing (`tag`, `badge`, `mr0..mr3` in `x1..x6`).
  - **Direct Process Switch (`sched_direct_switch`)**: Transfers message registers directly between trap frames and switches `TTBR0_EL1` + CPU context immediately without runqueue overhead.
  - Combined `SYS_IPC_REPLY_RECV` syscall that halves `EL0 <-> EL1` transitions for server loops.
- **Preemptive Scheduler & GICv2 / Generic Timer (`kernel/sched.c`, `kernel/gic.c`, `kernel/timer.c`)**:
  - ARM GICv2 distributor and CPU interface driver handling the 100 Hz ARMv8 Generic Physical Timer (PPI IRQ 30) and PL011 UART RX interrupts (SPI IRQ 33).
- **Isolated User-Mode (`EL0`) Services (`user/`)**:
  - **`uart_srv` (PID 1, `EL0`, `ASID 1`)**: User-space PL011 serial driver granted the `0x09000000` MMIO capability and page mapping (`VM_USER_DEVICE`), driving UART TX directly from `EL0`.
  - **`sysmon_srv` (PID 2, `EL0`, `ASID 2`)**: Telemetry, benchmark echo, and RPC compute service.
  - **`init_shell` (PID 3, `EL0`, `ASID 3`)**: Runs an automated boot self-test (10,000 cross-address-space IPC round-trip benchmark + MMU fault guard test) and provides an interactive shell (`helios@el0>`).

---

## Directory Structure

```text
ukernel/
├── Makefile            # Build & QEMU execution targets
├── linker.ld           # 4KB page-aligned AArch64 linker script
├── include/            # Kernel & user-space headers
│   ├── arch.h          # AArch64 system registers, barriers, trap_frame_t, cpu_context_t
│   ├── cap.h           # Capability types, rights bitmask, CSpace definitions
│   ├── gic.h           # ARM GICv2 interrupt controller interface
│   ├── ipc.h           # L4-style register IPC & endpoint structures
│   ├── kprintf.h       # Formatted kernel logging & panic
│   ├── mmu.h           # ARMv8-A 3-level 4KB page table & VSpace interface
│   ├── pmm.h           # Physical page frame allocator interface
│   ├── sched.h         # Preemptive scheduler & Task Control Block (TCB)
│   ├── syscall.h       # Syscall ABI numbers & telemetry structs
│   ├── timer.h         # ARMv8 Generic Physical Timer interface
│   ├── types.h         # Freestanding types & string primitives
│   ├── uart.h          # PL011 UART registers & IRQ ring buffer
│   └── user.h          # EL0 syscall wrappers & IPC protocol tags
├── kernel/             # Privileged EL1 microkernel core
│   ├── boot.S          # Entry point, EL3/EL2->EL1 drop, cpu_switch_to
│   ├── vectors.S       # 2KB-aligned exception vector table & trap entry/exit
│   ├── main.c          # Kernel initialization & EL0 service bootstrap
│   ├── pmm.c           # O(1) bitmap page frame allocator
│   ├── mmu.c           # 39-bit 3-level page tables & ASID switching
│   ├── cap.c           # Capability lookup & granting
│   ├── ipc.c           # Fastpath synchronous IPC implementation
│   ├── sched.c         # Priority/round-robin scheduler & task creation
│   ├── trap.c          # Syscall dispatcher, IRQ handler, MMU fault guard
│   ├── gic.c           # GICv2 driver
│   ├── timer.c         # Generic timer driver
│   ├── uart.c          # Early kernel UART & RX IRQ ring buffer
│   └── kprintf.c       # Freestanding printf & string utilities
└── user/               # Unprivileged EL0 user-space services
    ├── ulib.c          # EL0 syscall stubs & 24-byte register-packed console IPC
    ├── uart_srv.c      # PID 1: EL0 user-space PL011 UART driver server
    ├── sysmon_srv.c    # PID 2: EL0 system telemetry & RPC server
    └── shell.c         # PID 3: EL0 boot verification suite & interactive shell
```

---

## Building & Running

### Prerequisites (macOS / Linux)
Install `qemu`, `llvm`, and `lld`:
```bash
# macOS (Homebrew)
brew install qemu llvm lld
```

### Build & Boot
```bash
# Compile build/helios64.elf and build/helios64.bin
make

# Boot interactively in QEMU (type 'help', 'ps', 'mem', 'caps', 'bench', 'ping', 'fault', 'uptime', 'halt')
make run

# Run automated boot verification & shell smoke test
make test
```
