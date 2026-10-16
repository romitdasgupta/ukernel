#include "cap.h"
#include "sched.h"

void cap_init_task(struct task *t) {
    if (!t) return;
    memset(t->cspace, 0, sizeof(t->cspace));
}

int cap_grant(struct task *t, uint32_t slot, cap_type_t type, uint8_t rights,
              uint16_t badge, uint32_t obj_id, uintptr_t phys_base) {
    if (!t || slot >= MAX_CAPS_PER_TASK) {
        return EINVAL;
    }
    t->cspace[slot].type      = (uint8_t)type;
    t->cspace[slot].rights    = rights;
    t->cspace[slot].badge     = badge;
    t->cspace[slot].obj_id    = obj_id;
    t->cspace[slot].phys_base = phys_base;
    return EOK;
}

const capability_t *cap_lookup(struct task *t, uint32_t slot,
                               cap_type_t expected_type, uint8_t required_rights) {
    if (!t || slot >= MAX_CAPS_PER_TASK) {
        return NULL;
    }
    const capability_t *cap = &t->cspace[slot];
    if (cap->type != (uint8_t)expected_type) {
        return NULL;
    }
    if ((cap->rights & required_rights) != required_rights) {
        return NULL;
    }
    return cap;
}

const char *cap_type_name(uint8_t type) {
    switch ((cap_type_t)type) {
        case CAP_NONE:     return "NONE";
        case CAP_ENDPOINT: return "ENDPOINT";
        case CAP_FRAME:    return "FRAME";
        case CAP_DEVICE:   return "DEVICE";
        case CAP_TASK:     return "TASK";
        default:           return "UNKNOWN";
    }
}
