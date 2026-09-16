#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/task_wdt/task_wdt.h>

LOG_MODULE_REGISTER(l5_task1, LOG_LEVEL_DBG);

#define STACK_SIZE        2048
#define SENSOR_COUNT        20
#define SENSOR_PERIOD_MS   100

#define QUEUE_DEPTH          8
#define STUCK_DELAY_MS    3000
#define CONSUMER_WDT_MS    800

// 75 % of QUEUE_DEPTH
#define QUEUE_WARN_THRESH  ((QUEUE_DEPTH * 3U) / 4U)

/* ================================================================== */
/*  Message type                                                      */
/* ================================================================== */

struct sensor_data {
    int32_t temperature_mc;
    uint32_t timestamp_ms;
    uint8_t seq;
};

/* ================================================================== */
/*  Message queue (display_listener -> consumer_thread)               */
/* ================================================================== */

K_MSGQ_DEFINE(sensor_queue, sizeof(struct sensor_data), QUEUE_DEPTH, 4);

/* ================================================================== */
/*  Semaphores                                                        */
/* ================================================================== */

static K_SEM_DEFINE(demo_done, 0, 1);

/* ================================================================== */
/*  Forward declaration                                               */
/* ================================================================== */

static void display_listener_cb(const struct zbus_channel *chan);

/* ================================================================== */
/*  zbus observer & channel                                           */
/* ================================================================== */

ZBUS_LISTENER_DEFINE(display_listener, display_listener_cb);

ZBUS_CHAN_DEFINE(sensor_chan, struct sensor_data,
                 NULL, NULL,
                 ZBUS_OBSERVERS(display_listener),
                 ZBUS_MSG_INIT(.temperature_mc = 0,
                               .timestamp_ms = 0,
                               .seq = 0));

/* ================================================================== */
/*  Display listener – enqueues into sensor_queue                     */
/* ================================================================== */

static void display_listener_cb(const struct zbus_channel *chan)
{
    const struct sensor_data *msg =
        (const struct sensor_data *)zbus_chan_const_msg(chan);

    /*
     * Listener runs in publisher context.
     * Enqueue a copy for the consumer thread via the message queue.
     */
    int ret = k_msgq_put(&sensor_queue, msg, K_NO_WAIT);

    if (ret != 0) {
        LOG_WRN("[DISPLAY-LIS] queue FULL — dropped seq=%u (used=%u/%u)",
                msg->seq,
                k_msgq_num_used_get(&sensor_queue),
                QUEUE_DEPTH);
    } else {
        LOG_INF("[DISPLAY-LIS] enqueued seq=%u (used=%u/%u)",
                msg->seq,
                k_msgq_num_used_get(&sensor_queue),
                QUEUE_DEPTH);
    }
}

/* ================================================================== */
/*  Sensor timer (periodic publisher)                                  */
/* ================================================================== */

static K_SEM_DEFINE(timer_done, 0, 1);

static void timer_expiry(struct k_timer *timer)
{
    static int counter = 0;

    ARG_UNUSED(timer);

    struct sensor_data data = {
        .temperature_mc = 24000 + (counter * 350),
        .timestamp_ms = k_uptime_get_32(),
        .seq = (uint8_t)counter,
    };

    LOG_INF("[SENSOR] publish seq=%u temp=%d mC",
            data.seq, data.temperature_mc);

    int ret = zbus_chan_pub(&sensor_chan, &data, K_NO_WAIT);

    if (ret != 0) {
        LOG_WRN("[SENSOR] zbus publish failed ret=%d", ret);
    }

    counter++;

    if (counter >= SENSOR_COUNT) {
        k_sem_give(&timer_done);
        LOG_INF("[SENSOR] done publishing");
    }
}

K_TIMER_DEFINE(sensor_timer, timer_expiry, NULL);

/* ================================================================== */
/*  Task-watchdog callback                                            */
/* ================================================================== */

static void wdt_callback(int channel_id, void *user_data)
{
    ARG_UNUSED(user_data);

    LOG_ERR("[WDT] *** WATCHDOG FIRED channel=%d — consumer is STUCK! ***",
            channel_id);
    LOG_ERR("[WDT] queue fill at expiry: %u/%u",
            k_msgq_num_used_get(&sensor_queue), QUEUE_DEPTH);
    LOG_WRN("[WDT] (no system reset in demo — consumer will recover)");
}


/* ================================================================== */
/*  Consumer thread                                                   */
/* ================================================================== */

static void consumer_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    k_thread_name_set(k_current_get(), "consumer");

    // Register with the task watchdog
    int wdt_ch = task_wdt_add(CONSUMER_WDT_MS, wdt_callback, NULL);

    if (wdt_ch < 0) {
        LOG_ERR("[CONSUMER] task_wdt_add failed: %d", wdt_ch);
    } else {
        LOG_INF("[CONSUMER] WDT channel=%d timeout=%d ms",
                wdt_ch, CONSUMER_WDT_MS);
    }

    /*
     * Simulate a stuck consumer: sleep STUCK_DELAY_MS without calling
     * task_wdt_feed(). The queue fills and the watchdog fires.
     */
    LOG_WRN("[CONSUMER] *** SIMULATING STUCK STATE for %d ms ***",
            STUCK_DELAY_MS);
    k_sleep(K_MSEC(STUCK_DELAY_MS));
    LOG_INF("[CONSUMER] recovered —> resuming normal operation");

    // Normal consume loop
    int received = 0;

    while (received < SENSOR_COUNT) {
        // Feed the watchdog before blocking
        if (wdt_ch >= 0) {
            task_wdt_feed(wdt_ch);
        }

        struct sensor_data msg;
        int ret = k_msgq_get(&sensor_queue, &msg, K_MSEC(500));

        if (ret != 0) {
            // Check if producer already finished
            if (k_sem_take(&timer_done, K_NO_WAIT) == 0) {
                k_sem_give(&timer_done); /* restore for main */
                break;
            }
            continue;
        }

        received++;
        LOG_INF("[CONSUMER] seq=%u temp=%d mC latency=%u ms  [%d/%d]",
                msg.seq,
                msg.temperature_mc,
                k_uptime_get_32() - msg.timestamp_ms,
                received,
                SENSOR_COUNT);
    }

    if (wdt_ch >= 0) {
        task_wdt_feed(wdt_ch);
        task_wdt_delete(wdt_ch);
    }

    LOG_INF("[CONSUMER] done received=%d", received);
    k_sem_give(&demo_done);
}

/* ================================================================== */
/*  Health-check thread                                               */
/* ================================================================== */

static void health_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    k_thread_name_set(k_current_get(), "health");

    LOG_INF("[HEALTH] monitor started (warn at >= %u/%u = 75%%)",
            QUEUE_WARN_THRESH, QUEUE_DEPTH);

    // Deadline covers sensor publish time + stuck delay + 2 s margin
    uint32_t deadline = k_uptime_get_32()
                      + (uint32_t)(SENSOR_COUNT * SENSOR_PERIOD_MS)
                      + STUCK_DELAY_MS + 2000U;

    while (k_uptime_get_32() < deadline) {
        k_sleep(K_MSEC(200));

        uint32_t used  = k_msgq_num_used_get(&sensor_queue);
        uint32_t avail = QUEUE_DEPTH - used;

        if (used >= QUEUE_WARN_THRESH) {
            LOG_WRN("[HEALTH] *** WARNING queue=%u/%u (%u%%)! ***",
                    used, QUEUE_DEPTH,
                    (used * 100U) / QUEUE_DEPTH);
        } else {
            LOG_INF("[HEALTH] queue=%u/%u  free=%u",
                    used, QUEUE_DEPTH, avail);
        }
    }

    LOG_INF("[HEALTH] monitor done");
}

/* ================================================================== */
/*  Threads                                                           */
/* ================================================================== */

K_THREAD_DEFINE(consumer_thread, STACK_SIZE, consumer_thread_fn,
                NULL, NULL, NULL, 6, 0, 0);

K_THREAD_DEFINE(health_thread, STACK_SIZE, health_thread_fn,
                NULL, NULL, NULL, 7, 0, 0);

/* ================================================================== */
/*  Main                                                              */
/* ================================================================== */

int main(void)
{
    LOG_INF("=== L5 Task 1: Zbus + Task WDT + Health Monitor ===");
    LOG_INF("Sensor: every %d ms  x %d messages",
            SENSOR_PERIOD_MS, SENSOR_COUNT);
    LOG_INF("Queue: depth=%d  warn-threshold=%d (75%%)",
            QUEUE_DEPTH, QUEUE_WARN_THRESH);
    LOG_INF("Consumer: WDT timeout=%d ms  stuck-delay=%d ms",
            CONSUMER_WDT_MS, STUCK_DELAY_MS);
    LOG_INF("Expected: queue fills -> WDT fires -> consumer recovers");

    // Initialise task watchdog without HW fallback
    int ret = task_wdt_init(NULL);

    if (ret != 0) {
        LOG_ERR("task_wdt_init failed: %d", ret);
        return ret;
    }

    LOG_INF("Task watchdog initialized");

    // Start the periodic sensor publisher
    k_timer_start(&sensor_timer,
                  K_MSEC(SENSOR_PERIOD_MS),
                  K_MSEC(SENSOR_PERIOD_MS));

    // Wait for all messages to be published
    k_sem_take(&timer_done, K_FOREVER);
    k_timer_stop(&sensor_timer);

    LOG_INF("[MAIN] sensor done — waiting for consumer to finish");

    // Wait for the consumer thread to finish draining the queue
    k_sem_take(&demo_done, K_FOREVER);

    LOG_INF("[MAIN] demo complete");
    return 0;
}
