#include "ipc.h"
#include "cap.h"
#include "sched.h"
#include "kprintf.h"

static ipc_endpoint_t g_endpoints[MAX_ENDPOINTS];

void ipc_init(void) {
    memset(g_endpoints, 0, sizeof(g_endpoints));
}

int ipc_endpoint_create(uint32_t id, const char *name) {
    if (id == 0 || id >= MAX_ENDPOINTS) {
        return EINVAL;
    }
    ipc_endpoint_t *ep = &g_endpoints[id];
    ep->id              = id;
    ep->allocated       = true;
    ep->name            = name;
    ep->receiver        = NULL;
    ep->send_queue_head = NULL;
    ep->send_queue_tail = NULL;
    ep->total_msgs      = 0;
    ep->fastpath_hits   = 0;
    return EOK;
}

ipc_endpoint_t *ipc_get_endpoint(uint32_t id) {
    if (id == 0 || id >= MAX_ENDPOINTS || !g_endpoints[id].allocated) {
        return NULL;
    }
    return &g_endpoints[id];
}

/* Enqueue client task onto endpoint's FIFO send wait-queue */
static void ep_enqueue_sender(ipc_endpoint_t *ep, task_t *sender) {
    sender->ipc_next = NULL;
    if (!ep->send_queue_tail) {
        ep->send_queue_head = sender;
        ep->send_queue_tail = sender;
    } else {
        ep->send_queue_tail->ipc_next = sender;
        ep->send_queue_tail = sender;
    }
}

/* Dequeue next waiting client task from endpoint's FIFO send wait-queue */
static task_t *ep_dequeue_sender(ipc_endpoint_t *ep) {
    task_t *head = ep->send_queue_head;
    if (!head) return NULL;
    ep->send_queue_head = head->ipc_next;
    if (!ep->send_queue_head) {
        ep->send_queue_tail = NULL;
    }
    head->ipc_next = NULL;
    return head;
}

/*
 * Write IPC message directly into a receiving task's saved trap frame registers:
 *   x0 = EOK (0)
 *   x1 = tag
 *   x2 = badge (kernel-verified sender badge)
 *   x3 = mr0
 *   x4 = mr1
 *   x5 = mr2
 *   x6 = mr3
 */
ALWAYS_INLINE void deliver_to_trap_frame(trap_frame_t *dst_tf, uint64_t tag, uint64_t badge,
                                         uint64_t mr0, uint64_t mr1, uint64_t mr2, uint64_t mr3) {
    dst_tf->x[0] = (uint64_t)EOK;
    dst_tf->x[1] = tag;
    dst_tf->x[2] = badge;
    dst_tf->x[3] = mr0;
    dst_tf->x[4] = mr1;
    dst_tf->x[5] = mr2;
    dst_tf->x[6] = mr3;
}

/*
 * SYS_IPC_CALL: Client sends a message in registers and blocks waiting for Server's reply.
 * Input registers:
 *   x0 = cap_slot
 *   x1 = tag
 *   x2 = mr0, x3 = mr1, x4 = mr2, x5 = mr3
 */
void sys_ipc_call_handler(trap_frame_t *tf, uint32_t cap_slot) {
    task_t *client = sched_current();
    const capability_t *cap = cap_lookup(client, cap_slot, CAP_ENDPOINT, CAP_RIGHT_SEND);
    if (!cap) {
        tf->x[0] = (uint64_t)EPERM;
        return;
    }
    ipc_endpoint_t *ep = ipc_get_endpoint(cap->obj_id);
    if (!ep) {
        tf->x[0] = (uint64_t)ENOENT;
        return;
    }

    uint64_t tag   = tf->x[1];
    uint64_t badge = ((uint64_t)cap->badge << 16) | (client->id & 0xFFFFU);
    uint64_t mr0   = tf->x[2];
    uint64_t mr1   = tf->x[3];
    uint64_t mr2   = tf->x[4];
    uint64_t mr3   = tf->x[5];

    client->tf = tf;
    client->ipc_sent++;
    ep->total_msgs++;

    /* ========================================================================
     * L4 FASTPATH: Server is already blocked in RECV on this endpoint!
     * Directly transfer message registers into Server's trap frame and
     * perform an immediate direct context switch without generic scheduler overhead.
     * ======================================================================== */
    task_t *server = ep->receiver;
    if (server && server->state == TASK_BLOCKED_RECV) {
        ep->receiver = NULL;
        ep->fastpath_hits++;

        server->caller = client;
        server->ipc_rcvd++;
        server->blocked_ep_id = 0;

        deliver_to_trap_frame(server->tf, tag, badge, mr0, mr1, mr2, mr3);

        client->state = TASK_BLOCKED_REPL;
        client->blocked_ep_id = ep->id;

        sched_direct_switch(server);
        return;
    }

    /* Slowpath: Server is busy, stage message in client TCB and enqueue on endpoint */
    client->ipc_msg.tag   = tag;
    client->ipc_msg.badge = badge;
    client->ipc_msg.mr[0] = mr0;
    client->ipc_msg.mr[1] = mr1;
    client->ipc_msg.mr[2] = mr2;
    client->ipc_msg.mr[3] = mr3;

    client->state = TASK_BLOCKED_SEND;
    client->blocked_ep_id = ep->id;
    ep_enqueue_sender(ep, client);
    sched_yield();
}

/*
 * SYS_IPC_RECV: Server waits for a message on an endpoint.
 * Input registers:
 *   x0 = cap_slot
 */
void sys_ipc_recv_handler(trap_frame_t *tf, uint32_t cap_slot) {
    task_t *server = sched_current();
    const capability_t *cap = cap_lookup(server, cap_slot, CAP_ENDPOINT, CAP_RIGHT_RECV);
    if (!cap) {
        tf->x[0] = (uint64_t)EPERM;
        return;
    }
    ipc_endpoint_t *ep = ipc_get_endpoint(cap->obj_id);
    if (!ep) {
        tf->x[0] = (uint64_t)ENOENT;
        return;
    }

    server->tf = tf;

    /* Check if a client is already queued waiting to send */
    task_t *sender = ep_dequeue_sender(ep);
    if (sender) {
        server->ipc_rcvd++;
        deliver_to_trap_frame(tf,
                              sender->ipc_msg.tag,
                              sender->ipc_msg.badge,
                              sender->ipc_msg.mr[0],
                              sender->ipc_msg.mr[1],
                              sender->ipc_msg.mr[2],
                              sender->ipc_msg.mr[3]);

        if (sender->state == TASK_BLOCKED_SEND) {
            /* Sender performed a synchronous CALL -> transition sender to wait for REPLY */
            sender->state = TASK_BLOCKED_REPL;
            server->caller = sender;
        }
        return;
    }

    /* No sender waiting: block server on endpoint until a client calls */
    ep->receiver = server;
    server->state = TASK_BLOCKED_RECV;
    server->blocked_ep_id = ep->id;
    sched_yield();
}

/*
 * SYS_IPC_REPLY_RECV: Combined L4 Server primitive!
 * Atomically replies to the last caller AND blocks waiting for the next client request
 * in a single system call (halving kernel entry/exit overhead for servers).
 * Input registers:
 *   x0 = cap_slot
 *   x1 = reply_tag
 *   x2 = mr0, x3 = mr1, x4 = mr2, x5 = mr3
 */
void sys_ipc_reply_recv_handler(trap_frame_t *tf, uint32_t cap_slot) {
    task_t *server = sched_current();
    const capability_t *cap = cap_lookup(server, cap_slot, CAP_ENDPOINT, CAP_RIGHT_RECV);
    if (!cap) {
        tf->x[0] = (uint64_t)EPERM;
        return;
    }
    ipc_endpoint_t *ep = ipc_get_endpoint(cap->obj_id);
    if (!ep) {
        tf->x[0] = (uint64_t)ENOENT;
        return;
    }

    server->tf = tf;
    task_t *caller = server->caller;
    server->caller = NULL;

    if (caller && caller->state == TASK_BLOCKED_REPL) {
        uint64_t r_tag = tf->x[1];
        uint64_t r_mr0 = tf->x[2];
        uint64_t r_mr1 = tf->x[3];
        uint64_t r_mr2 = tf->x[4];
        uint64_t r_mr3 = tf->x[5];

        server->ipc_sent++;
        caller->ipc_rcvd++;
        caller->blocked_ep_id = 0;

        deliver_to_trap_frame(caller->tf, r_tag, server->id, r_mr0, r_mr1, r_mr2, r_mr3);

        /* Check if another sender is already queued on this endpoint */
        task_t *next_sender = ep_dequeue_sender(ep);
        if (next_sender) {
            caller->state = TASK_READY;
            server->ipc_rcvd++;
            deliver_to_trap_frame(tf,
                                  next_sender->ipc_msg.tag,
                                  next_sender->ipc_msg.badge,
                                  next_sender->ipc_msg.mr[0],
                                  next_sender->ipc_msg.mr[1],
                                  next_sender->ipc_msg.mr[2],
                                  next_sender->ipc_msg.mr[3]);
            next_sender->state = TASK_BLOCKED_REPL;
            server->caller = next_sender;
            return;
        }

        /* ====================================================================
         * L4 FASTPATH REPLY: No other sender queued — park server as receiver
         * on the endpoint and switch directly back to the caller!
         * ==================================================================== */
        ep->receiver = server;
        ep->fastpath_hits++;
        server->state = TASK_BLOCKED_RECV;
        server->blocked_ep_id = ep->id;

        sched_direct_switch(caller);
        return;
    }

    /* No caller to reply to; fall back to normal receive */
    sys_ipc_recv_handler(tf, cap_slot);
}

/*
 * SYS_IPC_SEND: One-way synchronous send (does not wait for reply).
 */
void sys_ipc_send_handler(trap_frame_t *tf, uint32_t cap_slot) {
    task_t *sender = sched_current();
    const capability_t *cap = cap_lookup(sender, cap_slot, CAP_ENDPOINT, CAP_RIGHT_SEND);
    if (!cap) {
        tf->x[0] = (uint64_t)EPERM;
        return;
    }
    ipc_endpoint_t *ep = ipc_get_endpoint(cap->obj_id);
    if (!ep) {
        tf->x[0] = (uint64_t)ENOENT;
        return;
    }

    uint64_t tag   = tf->x[1];
    uint64_t badge = ((uint64_t)cap->badge << 16) | (sender->id & 0xFFFFU);
    uint64_t mr0   = tf->x[2];
    uint64_t mr1   = tf->x[3];
    uint64_t mr2   = tf->x[4];
    uint64_t mr3   = tf->x[5];

    sender->tf = tf;
    sender->ipc_sent++;
    ep->total_msgs++;

    task_t *server = ep->receiver;
    if (server && server->state == TASK_BLOCKED_RECV) {
        ep->receiver = NULL;
        ep->fastpath_hits++;
        server->caller = NULL;
        server->ipc_rcvd++;
        server->blocked_ep_id = 0;

        deliver_to_trap_frame(server->tf, tag, badge, mr0, mr1, mr2, mr3);
        tf->x[0] = EOK;
        server->state = TASK_READY;
        sched_direct_switch(server);
        return;
    }

    tf->x[0] = (uint64_t)EAGAIN;
}
