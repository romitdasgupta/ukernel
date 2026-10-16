#include "kprintf.h"
#include "uart.h"
#include "arch.h"

typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, l)   __builtin_va_arg(v, l)

/* ============================================================================
 * Freestanding String & Memory Primitives
 * ============================================================================ */
void *memset(void *dst, int c, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    uint8_t val = (uint8_t)c;
    while (n--) {
        *p++ = val;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s && s[len]) {
        len++;
    }
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return (int)(uint8_t)*s1 - (int)(uint8_t)*s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return (int)(uint8_t)*s1 - (int)(uint8_t)*s2;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    for (; i < n; i++) {
        dst[i] = '\0';
    }
    return dst;
}

/* ============================================================================
 * Formatted Kernel Output (kprintf)
 * Supports: %s, %c, %d, %u, %ld, %lu, %x, %lx, %p, and width/zero-padding
 * ============================================================================ */
static void print_num(uint64_t val, int base, bool is_signed, int width, char pad_char) {
    char buf[32];
    const char *digits = "0123456789abcdef";
    bool neg = false;
    int i = 0;

    if (is_signed && (int64_t)val < 0) {
        neg = true;
        val = (uint64_t)(-(int64_t)val);
    }

    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val > 0) {
            buf[i++] = digits[val % (uint64_t)base];
            val /= (uint64_t)base;
        }
    }

    int total_len = i + (neg ? 1 : 0);
    if (neg && pad_char == '0') {
        uart_putc('-');
    }
    while (total_len < width) {
        uart_putc(pad_char);
        total_len++;
    }
    if (neg && pad_char != '0') {
        uart_putc('-');
    }
    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

static void kvprintf(const char *fmt, va_list ap) {
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            uart_putc(*fmt);
            continue;
        }
        fmt++;
        char pad_char = ' ';
        int width = 0;
        bool left_align = false;

        if (*fmt == '-') {
            left_align = true;
            fmt++;
        }
        if (*fmt == '0') {
            pad_char = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        bool is_long = false;
        if (*fmt == 'l') {
            is_long = true;
            fmt++;
            if (*fmt == 'l') {
                fmt++;
            }
        }

        switch (*fmt) {
            case 'c': {
                char c = (char)va_arg(ap, int);
                uart_putc(c);
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                int len = (int)strlen(s);
                if (!left_align) {
                    while (len < width) {
                        uart_putc(' ');
                        len++;
                    }
                }
                uart_puts(s);
                if (left_align) {
                    while (len < width) {
                        uart_putc(' ');
                        len++;
                    }
                }
                break;
            }
            case 'd': {
                int64_t v = is_long ? va_arg(ap, int64_t) : (int64_t)va_arg(ap, int);
                print_num((uint64_t)v, 10, true, width, pad_char);
                break;
            }
            case 'u': {
                uint64_t v = is_long ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned int);
                print_num(v, 10, false, width, pad_char);
                break;
            }
            case 'x': {
                uint64_t v = is_long ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned int);
                print_num(v, 16, false, width, pad_char);
                break;
            }
            case 'p': {
                uint64_t v = (uint64_t)va_arg(ap, void *);
                uart_puts("0x");
                print_num(v, 16, false, 16, '0');
                break;
            }
            case '%': {
                uart_putc('%');
                break;
            }
            default: {
                uart_putc('%');
                uart_putc(*fmt);
                break;
            }
        }
    }
}

void kprintf(const char *fmt, ...) {
    uint64_t flags = irq_save();
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    irq_restore(flags);
}

void kpanic(const char *fmt, ...) {
    irq_disable();
    va_list ap;
    va_start(ap, fmt);
    uart_puts("\n" CLR_RED CLR_BOLD "[KERNEL PANIC] ");
    kvprintf(fmt, ap);
    uart_puts(CLR_RESET "\nSystem halted.\n");
    va_end(ap);
    for (;;) {
        wfi();
    }
}
