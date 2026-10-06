#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/tracing/tracing.h>

LOG_MODULE_REGISTER(homework, LOG_LEVEL_INF);

// Set to 1 after proving the deadline miss.
#ifndef FIX_CONTROL_PRIORITY
#define FIX_CONTROL_PRIORITY 0
#endif

#define STACK_SIZE            2048
#define EVENT_PERIOD_MS        250
#define MAINTENANCE_LOAD_US  45000

// Every control event must be picked up within DEADLINE_MS
#define DEADLINE_MS             10

#if FIX_CONTROL_PRIORITY
// Corrected
#define CONTROL_PRIORITY          3
#define MAINTENANCE_PRIORITY      4
#else
// Failure
#define CONTROL_PRIORITY          7
#define MAINTENANCE_PRIORITY      4
#endif

// Log each of the first misses individually, then only every 10th miss.
#define MISS_WARN_FIRST         5
#define MISS_WARN_EVERY        10

struct control_event {
    uint32_t seq;
    uint32_t ready_ms;
};

K_MSGQ_DEFINE(control_queue, sizeof(struct control_event), 4, 4);
K_SEM_DEFINE(maintenance_start, 0, 1);

/* ================================================================== */
/*  Timer expiry: creates one control event                           */
/* ================================================================== */

static void event_timer_expiry(struct k_timer *timer)
{
    ARG_UNUSED(timer);

    static uint32_t seq;
    struct control_event event = {
        .seq = seq++,
        .ready_ms = k_uptime_get_32(),
    };

    /* Timer expiry runs in interrupt context, so never wait here. */
    int ret = k_msgq_put(&control_queue, &event, K_NO_WAIT);

    if (ret != 0) {
        return;
    }

    /* Both threads become ready when the timer interrupt returns. */
    k_sem_give(&maintenance_start);

    sys_trace_named_event("event_ready", event.seq, seq);
}

K_TIMER_DEFINE(event_timer, event_timer_expiry, NULL);

/* ================================================================== */
/*  Control thread                                                    */
/* ================================================================== */

static void control_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    uint32_t deadline_misses = 0;

    while (true) {
        struct control_event event;
        int ret = k_msgq_get(&control_queue, &event, K_FOREVER);

        if (ret != 0) {
            LOG_ERR("[CONTROL] receive failed: %d", ret);
            continue;
        }

        uint32_t latency_ms = k_uptime_get_32() - event.ready_ms;

        LOG_INF("[CONTROL] processed seq=%u", event.seq);

        sys_trace_named_event("event_done", event.seq, latency_ms);

        if (latency_ms > DEADLINE_MS) {
            deadline_misses++;

            if (deadline_misses <= MISS_WARN_FIRST || (deadline_misses % MISS_WARN_EVERY) == 0) {
                LOG_WRN("[CONTROL] deadline_miss count=%u seq=%u "
                        "latency=%ums (deadline=%ums)",
                        deadline_misses, event.seq, latency_ms,
                        DEADLINE_MS);
            }
        } else {
            LOG_INF("[CONTROL] within deadline seq=%u latency=%ums",
                    event.seq, latency_ms);
        }
    }
}

/* ================================================================== */
/*  Background maintenance thread                                    */
/* ================================================================== */

static void maintenance_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    while (true) {
        k_sem_take(&maintenance_start, K_FOREVER);

        /* This work is important, but it has no short deadline. */
        k_busy_wait(MAINTENANCE_LOAD_US);
    }
}

K_THREAD_DEFINE(control, STACK_SIZE, control_fn,
                NULL, NULL, NULL, CONTROL_PRIORITY, 0, 0);

K_THREAD_DEFINE(maintenance, STACK_SIZE, maintenance_fn,
                NULL, NULL, NULL, MAINTENANCE_PRIORITY, 0, 0);

int main(void)
{
    LOG_INF("=== L6 Homework: Runtime Investigation ===");
    LOG_INF("Control work must start within %d ms", DEADLINE_MS);
    LOG_INF("Control priority=%d, maintenance priority=%d",
            CONTROL_PRIORITY, MAINTENANCE_PRIORITY);

#if FIX_CONTROL_PRIORITY
    LOG_INF("Corrected: control runs before maintenance load");
#else
    LOG_INF("Failure: maintenance load runs before control");
    LOG_INF("Inspect, measure, trace, explain, and correct the delay");
#endif

    k_timer_start(&event_timer, K_MSEC(500), K_MSEC(EVENT_PERIOD_MS));

    return 0;
}