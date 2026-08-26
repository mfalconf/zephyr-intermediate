/*
 * Lecture 3 - Homework Starter Code
 *
 * GOAL: Convert a polling loop to an event-driven workqueue architecture.
 *
 * The starter code works but is INEFFICIENT.
 * polling_thread wakes every 10ms to check a flag.
 * sensor_sim fires every 100ms - that's 10 wasted wake-ups per event.
 *
 *
 * ================================================================
 * TASKS
 * ================================================================
 *
 * TASK 1 (starter - already works, just run it):
 *   Run the starter. Count wake-ups vs real events in the log.
 *   Expected: ~10 wake-ups per sensor event. Confirm this.
 *
 * TASK 2 (implement):
 *   Replace polling_thread with a k_work handler.
 *   sensor_sim should call k_work_submit() instead of setting a flag.
 *   The handler should do what polling_thread currently does.
 *
 *   Steps:
 *   - Define a work item with K_WORK_DEFINE
 *   - Write the handler function
 *   - In sensor_sim: call k_work_submit() (remove k_sem_give + flag)
 *   - Remove the polling_thread entirely
 *
 * TASK 3 (verify):
 *   Add k_uptime_get_32() to your handler's LOG_INF.
 *   Confirm handler runs only when sensor_sim fires (every ~100ms).
 *   No unnecessary wake-ups.
 *
 * BONUS (debounce):
 *   Change sensor_sim to fire 5 events within 20ms (not 1 per 100ms).
 *   Use k_work_reschedule with 30ms delay so only ONE handler
 *   call occurs after the burst - not 5.
 *   Log the reschedule timestamps to confirm the burst collapses.
 *
 * ================================================================
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdbool.h>

#define TEST_BONUS 1

LOG_MODULE_REGISTER(homework, LOG_LEVEL_DBG);

#define STACK_SIZE    1024
#define SENSOR_MS     100    /* sensor fires every 100ms */
#define EVENT_COUNT   10     /* total sensor events to produce */

/* ================================================================
 * STARTER CODE -- inefficient polling version
 * Run this first, then replace with workqueue in Task 2.
 * ================================================================ */

/* Statistics */
static int total_events;
static int total_processed;

static void sensor_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    total_processed++;
    LOG_INF("[HANDLER] processed event %d  tick=%u",
            total_processed, k_uptime_get_32());
}

#if TEST_BONUS
K_WORK_DELAYABLE_DEFINE(debounce_work, sensor_handler);
#else
K_WORK_DEFINE(sensor_work, sensor_handler);
#endif


#if TEST_BONUS
static void simulate_event_burst()
{
  for(int i = 0; i < 5; i++)
  {
    k_msleep(4);
    k_work_reschedule(&debounce_work, K_MSEC(30));
    LOG_INF("[EVENT_BURST] event %d  tick=%u", i, k_uptime_get_32());
  }
}
#endif
/* ------------------------------------------------------------------ */
/*  sensor_sim_fn - fires EVENT_COUNT events, 100ms apart               */
/* ------------------------------------------------------------------ */

static void sensor_sim_fn(void *p1, void *p2, void *p3)
{
    for (int i = 0; i < EVENT_COUNT; i++) {
#if TEST_BONUS
        simulate_event_burst();
        total_events++;
        k_msleep(50);
#else
        k_msleep(SENSOR_MS);
        int ret = k_work_submit(&sensor_work);
        if (ret < 0) { LOG_ERR("submit failed: %d", ret); }
        total_events++;
        LOG_INF("[SENSOR] event %d  tick=%u", i+ 1, k_uptime_get_32());
#endif        
    }

    LOG_INF("[SENSOR] all events produced");

    LOG_INF("\n");
    LOG_INF("[SUMMARY] events=%d  processed=%d\n",
            total_events,
            total_processed);
}

K_THREAD_DEFINE(sensor_thread,  STACK_SIZE, sensor_sim_fn, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
    LOG_INF("=== L3 Homework: Polling to Workqueue ===");
    LOG_INF("< TEST_BONUS FLAG = %d >", TEST_BONUS);
#if !TEST_BONUS 
    LOG_INF("Sensor fires every %dms", SENSOR_MS);
#endif    
    /* Wait long enough for all events to complete */
    k_msleep((EVENT_COUNT + 2) * SENSOR_MS + 500);

    return 0;
}
