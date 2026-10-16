#ifndef UKERNEL_KPRINTF_H
#define UKERNEL_KPRINTF_H

#include "types.h"

/* ANSI color codes for clean boot diagnostics */
#define CLR_RESET   "\033[0m"
#define CLR_BOLD    "\033[1m"
#define CLR_DIM     "\033[2m"
#define CLR_GREEN   "\033[32m"
#define CLR_CYAN    "\033[36m"
#define CLR_YELLOW  "\033[33m"
#define CLR_RED     "\033[31m"
#define CLR_MAGENTA "\033[35m"

void kprintf(const char *fmt, ...);
void kpanic(const char *fmt, ...) NORETURN;

#endif /* UKERNEL_KPRINTF_H */
