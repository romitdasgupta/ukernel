#include "user.h"
#include "arch.h"

typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, l)   __builtin_va_arg(v, l)

/* ============================================================================
 * Raw AArch64 EL0 System Call Invocations ('svc #0')
 * ============================================================================ */
void sys_yield(void) {
    register uint64_t x8 __asm__("x8") = SYS_YIELD;
    __asm__ volatile("svc #0" :: "r"(x8) : "x0", "memory");
}

void sys_sleep_ms(uint64_t ms) {
    register uint64_t x0 __asm__("x0") = ms;
    register uint64_t x8 __asm__("x8") = SYS_SLEEP_MS;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
}

int sys_ipc_call(uint32_t cap_slot, uint64_t tag,
                 uint64_t mr0, uint64_t mr1, uint64_t mr2, uint64_t mr3,
                 ipc_msg_t *reply) {
    register uint64_t x0 __asm__("x0") = cap_slot;
    register uint64_t x1 __asm__("x1") = tag;
    register uint64_t x2 __asm__("x2") = mr0;
    register uint64_t x3 __asm__("x3") = mr1;
    register uint64_t x4 __asm__("x4") = mr2;
    register uint64_t x5 __asm__("x5") = mr3;
    register uint64_t x6 __asm__("x6") = 0;
    register uint64_t x8 __asm__("x8") = SYS_IPC_CALL;

    __asm__ volatile(
        "svc #0"
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x6)
        : "r"(x8)
        : "memory"
    );

    if (reply) {
        reply->tag   = x1;
        reply->badge = x2;
        reply->mr[0] = x3;
        reply->mr[1] = x4;
        reply->mr[2] = x5;
        reply->mr[3] = x6;
    }
    return (int)x0;
}

int sys_ipc_recv(uint32_t cap_slot, ipc_msg_t *msg) {
    register uint64_t x0 __asm__("x0") = cap_slot;
    register uint64_t x1 __asm__("x1") = 0;
    register uint64_t x2 __asm__("x2") = 0;
    register uint64_t x3 __asm__("x3") = 0;
    register uint64_t x4 __asm__("x4") = 0;
    register uint64_t x5 __asm__("x5") = 0;
    register uint64_t x6 __asm__("x6") = 0;
    register uint64_t x8 __asm__("x8") = SYS_IPC_RECV;

    __asm__ volatile(
        "svc #0"
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x6)
        : "r"(x8)
        : "memory"
    );

    if (msg) {
        msg->tag   = x1;
        msg->badge = x2;
        msg->mr[0] = x3;
        msg->mr[1] = x4;
        msg->mr[2] = x5;
        msg->mr[3] = x6;
    }
    return (int)x0;
}

int sys_ipc_reply_recv(uint32_t cap_slot, uint64_t reply_tag,
                       uint64_t mr0, uint64_t mr1, uint64_t mr2, uint64_t mr3,
                       ipc_msg_t *next_msg) {
    register uint64_t x0 __asm__("x0") = cap_slot;
    register uint64_t x1 __asm__("x1") = reply_tag;
    register uint64_t x2 __asm__("x2") = mr0;
    register uint64_t x3 __asm__("x3") = mr1;
    register uint64_t x4 __asm__("x4") = mr2;
    register uint64_t x5 __asm__("x5") = mr3;
    register uint64_t x6 __asm__("x6") = 0;
    register uint64_t x8 __asm__("x8") = SYS_IPC_REPLY_RECV;

    __asm__ volatile(
        "svc #0"
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x6)
        : "r"(x8)
        : "memory"
    );

    if (next_msg) {
        next_msg->tag   = x1;
        next_msg->badge = x2;
        next_msg->mr[0] = x3;
        next_msg->mr[1] = x4;
        next_msg->mr[2] = x5;
        next_msg->mr[3] = x6;
    }
    return (int)x0;
}

int sys_get_task_info(uint32_t task_id, task_info_t *out) {
    register uint64_t x0 __asm__("x0") = task_id;
    register uint64_t x1 __asm__("x1") = (uintptr_t)out;
    register uint64_t x8 __asm__("x8") = SYS_GET_TASK_INFO;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x8) : "memory");
    return (int)x0;
}

int sys_get_sys_info(sys_info_t *out) {
    register uint64_t x0 __asm__("x0") = (uintptr_t)out;
    register uint64_t x8 __asm__("x8") = SYS_GET_SYS_INFO;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return (int)x0;
}

int sys_cap_inspect(uint32_t slot, cap_info_t *out) {
    register uint64_t x0 __asm__("x0") = slot;
    register uint64_t x1 __asm__("x1") = (uintptr_t)out;
    register uint64_t x8 __asm__("x8") = SYS_CAP_INSPECT;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x8) : "memory");
    return (int)x0;
}

int sys_fault_guard(uintptr_t recovery_pc) {
    register uint64_t x0 __asm__("x0") = recovery_pc;
    register uint64_t x8 __asm__("x8") = SYS_FAULT_GUARD;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return (int)x0;
}

void sys_shutdown(void) {
    register uint64_t x8 __asm__("x8") = SYS_SHUTDOWN;
    __asm__ volatile("svc #0" :: "r"(x8) : "memory");
    for (;;) {}
}

/* ============================================================================
 * Zero-Copy Register-Packed Console IPC Client (talks to EL0 uart_srv)
 * Packs up to 24 bytes per IPC call into hardware registers mr1..mr3!
 * ============================================================================ */
void u_putc(char c) {
    sys_ipc_call(CAP_SLOT_UART_EP, UART_IPC_PUTC, (uint8_t)c, 0, 0, 0, NULL);
}

void u_puts(const char *s) {
    if (!s) return;
    while (*s) {
        uint64_t mr[3] = {0, 0, 0};
        uint8_t *bytes = (uint8_t *)mr;
        uint64_t chunk_len = 0;
        while (*s && chunk_len < 24) {
            bytes[chunk_len++] = (uint8_t)(*s++);
        }
        sys_ipc_call(CAP_SLOT_UART_EP, UART_IPC_WRITE_STR, chunk_len, mr[0], mr[1], mr[2], NULL);
    }
}

int u_getc(void) {
    ipc_msg_t reply;
    int rc = sys_ipc_call(CAP_SLOT_UART_EP, UART_IPC_GETC, 0, 0, 0, 0, &reply);
    if (rc != EOK) return -1;
    return (int)(int64_t)reply.mr[0];
}

/* Buffered user-space formatted print over IPC */
typedef struct uprint_buf {
    char   data[24];
    size_t len;
} uprint_buf_t;

static void ubuf_flush(uprint_buf_t *b) {
    if (b->len == 0) return;
    uint64_t mr[3] = {0, 0, 0};
    memcpy(mr, b->data, b->len);
    sys_ipc_call(CAP_SLOT_UART_EP, UART_IPC_WRITE_STR, b->len, mr[0], mr[1], mr[2], NULL);
    b->len = 0;
}

static void ubuf_putc(uprint_buf_t *b, char c) {
    b->data[b->len++] = c;
    if (b->len == 24) {
        ubuf_flush(b);
    }
}

static void ubuf_puts(uprint_buf_t *b, const char *s) {
    while (*s) {
        ubuf_putc(b, *s++);
    }
}

static void ubuf_num(uprint_buf_t *b, uint64_t val, int base, bool is_signed, int width, char pad) {
    char tmp[32];
    const char *digits = "0123456789abcdef";
    bool neg = false;
    int i = 0;

    if (is_signed && (int64_t)val < 0) {
        neg = true;
        val = (uint64_t)(-(int64_t)val);
    }
    if (val == 0) {
        tmp[i++] = '0';
    } else {
        while (val > 0) {
            tmp[i++] = digits[val % (uint64_t)base];
            val /= (uint64_t)base;
        }
    }
    int total = i + (neg ? 1 : 0);
    if (neg && pad == '0') ubuf_putc(b, '-');
    while (total < width) {
        ubuf_putc(b, pad);
        total++;
    }
    if (neg && pad != '0') ubuf_putc(b, '-');
    while (i > 0) {
        ubuf_putc(b, tmp[--i]);
    }
}

void u_printf(const char *fmt, ...) {
    uprint_buf_t buf = {{0}, 0};
    va_list ap;
    va_start(ap, fmt);

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            ubuf_putc(&buf, *fmt);
            continue;
        }
        fmt++;
        char pad = ' ';
        int width = 0;
        bool left = false;
        if (*fmt == '-') { left = true; fmt++; }
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        bool is_long = false;
        if (*fmt == 'l') {
            is_long = true;
            fmt++;
            if (*fmt == 'l') fmt++;
        }
        switch (*fmt) {
            case 'c':
                ubuf_putc(&buf, (char)va_arg(ap, int));
                break;
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                int slen = (int)strlen(s);
                if (!left) {
                    while (slen < width) { ubuf_putc(&buf, ' '); slen++; }
                }
                ubuf_puts(&buf, s);
                if (left) {
                    while (slen < width) { ubuf_putc(&buf, ' '); slen++; }
                }
                break;
            }
            case 'd': {
                int64_t v = is_long ? va_arg(ap, int64_t) : (int64_t)va_arg(ap, int);
                ubuf_num(&buf, (uint64_t)v, 10, true, width, pad);
                break;
            }
            case 'u': {
                uint64_t v = is_long ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned int);
                ubuf_num(&buf, v, 10, false, width, pad);
                break;
            }
            case 'x': {
                uint64_t v = is_long ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned int);
                ubuf_num(&buf, v, 16, false, width, pad);
                break;
            }
            case '%':
                ubuf_putc(&buf, '%');
                break;
            default:
                ubuf_putc(&buf, '%');
                ubuf_putc(&buf, *fmt);
                break;
        }
    }

    va_end(ap);
    ubuf_flush(&buf);
}
