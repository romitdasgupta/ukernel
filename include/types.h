#ifndef UKERNEL_TYPES_H
#define UKERNEL_TYPES_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

typedef uint64_t            uintptr_t;
typedef int64_t            intptr_t;
typedef uint64_t           size_t;
typedef int64_t            ssize_t;

typedef _Bool              bool;
#define true               1
#define false              0
#define NULL               ((void *)0)

#define ALIGN_UP(x, a)     (((x) + ((uintptr_t)(a) - 1)) & ~((uintptr_t)(a) - 1))
#define ALIGN_DOWN(x, a)   ((x) & ~((uintptr_t)(a) - 1))
#define ARRAY_SIZE(arr)    (sizeof(arr) / sizeof((arr)[0]))

#define PACKED             __attribute__((packed))
#define ALIGNED(n)         __attribute__((aligned(n)))
#define NORETURN           __attribute__((noreturn))
#define ALWAYS_INLINE      static inline __attribute__((always_inline))
#define UNUSED             __attribute__((unused))

/* Standard error codes */
#define EOK                0
#define EINVAL            -1
#define ENOMEM            -2
#define EPERM             -3
#define ENOENT            -4
#define EAGAIN            -5
#define EFAULT            -6

/* Memory manipulation primitives */
void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
int   strcmp(const char *s1, const char *s2);
int   strncmp(const char *s1, const char *s2, size_t n);
size_t strlen(const char *s);
char *strncpy(char *dst, const char *src, size_t n);

#endif /* UKERNEL_TYPES_H */
