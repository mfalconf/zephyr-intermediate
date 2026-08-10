#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(l1_task_1_2, LOG_LEVEL_DBG);

#define STACK_SIZE 1024

#define PRIO_COOP -1
#define PRIO_COOP_WAIT_TICKS 500000

#define PRIO_LOW 7
#define PRIO_LOW_SLEEP_MS 300

#define PRIO_MED 5
#define PRIO_MED_SLEEP_MS 200

#define PRIO_HIGH 3
#define PRIO_HIGH_SLEEP_MS 100

void t_coop_fn(void *p1, void *p2, void *p3)
{
    LOG_INF("[T_COOP] starting - will run 5 steps without yielding");

    for (int i = 0; i < 5; i++) {
        k_busy_wait(PRIO_COOP_WAIT_TICKS);   
        LOG_INF("[T_COOP] step %d/5 - still holding CPU  tick=%u",
                i + 1, k_uptime_get_32());
    }

    LOG_INF("[T_COOP] yielding now!");
    k_yield();

    LOG_INF("[T_COOP] done");
}

void t_low_fn(void *p1, void *p2, void *p3)
{
    int step = 0;

    while (1) {
        LOG_INF("T_LOW running : step %d  tick=%u", step++, k_uptime_get_32());
        k_msleep(PRIO_LOW_SLEEP_MS);
    }
}

void t_med_fn(void *p1, void *p2, void *p3)
{
    int step = 0;

    while (1) {
        LOG_INF("T_MED running : step %d  tick=%u", step++, k_uptime_get_32());
        k_msleep(PRIO_MED_SLEEP_MS);
    }
}

void t_high_fn(void *p1, void *p2, void *p3)
{
    int step = 0;

    while (1) {
        LOG_INF("T_HIGH running : step %d  tick=%u", step++, k_uptime_get_32());
        k_msleep(PRIO_HIGH_SLEEP_MS);
    }
}

K_THREAD_DEFINE(T_LOW, STACK_SIZE, t_low_fn,
                NULL, NULL, NULL, PRIO_LOW, 0, 0);
K_THREAD_DEFINE(T_MED, STACK_SIZE, t_med_fn,
                NULL, NULL, NULL, PRIO_MED, 0, 0);
K_THREAD_DEFINE(T_HIGH, STACK_SIZE, t_high_fn,
                NULL, NULL, NULL, PRIO_HIGH, 0, 0);
K_THREAD_DEFINE(T_COOP, STACK_SIZE, t_coop_fn,
                NULL, NULL, NULL, PRIO_COOP, 0, 0);


int main(void)
{
    LOG_INF("=== L1 Task 1.2: Scheduling Competition ===");
    LOG_INF("Thread T_LOW: priority %d, sleeps %dms", PRIO_LOW, PRIO_LOW_SLEEP_MS);
    LOG_INF("Thread T_MED: priority %d, sleeps %dms", PRIO_MED, PRIO_MED_SLEEP_MS);
    LOG_INF("Thread T_HIGH: priority %d, sleeps %dms", PRIO_HIGH, PRIO_HIGH_SLEEP_MS);
    LOG_INF("Thread T_COOP: priority %d", PRIO_COOP);
    return 0;
}
