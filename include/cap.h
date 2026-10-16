#ifndef UKERNEL_CAP_H
#define UKERNEL_CAP_H

#include "types.h"

#define MAX_CAPS_PER_TASK   16

/* Capability Object Types */
typedef enum cap_type {
    CAP_NONE     = 0,
    CAP_ENDPOINT = 1,   /* Synchronous IPC Endpoint */
    CAP_FRAME    = 2,   /* Physical Memory Page Frame */
    CAP_DEVICE   = 3,   /* MMIO Device Region */
    CAP_TASK     = 4,   /* Task Control Block Authority */
} cap_type_t;

/* Capability Access Rights Bitmask */
#define CAP_RIGHT_READ      (1U << 0)
#define CAP_RIGHT_WRITE     (1U << 1)
#define CAP_RIGHT_SEND      (1U << 2)
#define CAP_RIGHT_RECV      (1U << 3)
#define CAP_RIGHT_GRANT     (1U << 4)
#define CAP_RIGHT_ALL       (0x1FU)

/* Well-known capability slots in each user task's CSpace */
#define CAP_SLOT_SELF_TASK  0   /* Authority to inspect own task */
#define CAP_SLOT_UART_EP    1   /* IPC Endpoint for User-Space UART Server */
#define CAP_SLOT_SYSMON_EP  2   /* IPC Endpoint for System Telemetry Server */
#define CAP_SLOT_DEVICE_0   3   /* Optional MMIO Device capability (for driver tasks) */

typedef struct capability {
    uint8_t    type;        /* cap_type_t */
    uint8_t    rights;      /* CAP_RIGHT_* bitmask */
    uint16_t   badge;       /* Unforgeable sender badge attached by kernel on IPC */
    uint32_t   obj_id;      /* Endpoint ID, Task ID, or size in pages */
    uintptr_t  phys_base;   /* Physical base address (for CAP_FRAME / CAP_DEVICE) */
} capability_t;

struct task;

void cap_init_task(struct task *t);
int  cap_grant(struct task *t, uint32_t slot, cap_type_t type, uint8_t rights,
               uint16_t badge, uint32_t obj_id, uintptr_t phys_base);
const capability_t *cap_lookup(struct task *t, uint32_t slot, cap_type_t expected_type, uint8_t required_rights);
const char *cap_type_name(uint8_t type);

#endif /* UKERNEL_CAP_H */
