#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(l2_task1, LOG_LEVEL_DBG);

#define STACK_SIZE      1024
#define PRIO            5
#define INCREMENTS      500000   /* each thread increments this many times */

/* Shared state - intentionally unprotected */
static volatile uint32_t counter;

static struct k_sem wait_sem;
static struct k_sem done_sem;

static K_MUTEX_DEFINE(counter_mutex);

void worker_fn(void *p1, void *p2, void *p3)
{
    const char *name = k_thread_name_get(k_current_get());
    
    k_sem_take(&wait_sem, K_FOREVER);

    for (int i = 0; i < INCREMENTS; i++) {
        counter++;
    }

    LOG_INF("[%s] finished", name);
    k_sem_give(&done_sem);

    k_sem_take(&wait_sem, K_FOREVER);

    for (int i = 0; i < INCREMENTS; i++) {
        k_mutex_lock(&counter_mutex, K_FOREVER);
        counter++;
        k_mutex_unlock(&counter_mutex);
    }

    LOG_INF("[%s] finished with mutex", name);
    k_sem_give(&done_sem);
}

K_THREAD_DEFINE(worker_a, STACK_SIZE, worker_fn, NULL, NULL, NULL,
                PRIO, 0, 0);
K_THREAD_DEFINE(worker_b, STACK_SIZE, worker_fn, NULL, NULL, NULL,
                PRIO, 0, 0);

void wait_sec(unsigned int times) {
    times *= 2;
    while(times--) {
        k_msleep(500);
        LOG_INF("Waiting: %d", times);
    }
    LOG_INF("Ready!");
    k_sem_give(&wait_sem);
    k_sem_give(&wait_sem);
}

int main(void)
{
    k_sem_init(&wait_sem, 0, 2);
    k_sem_init(&done_sem, 0, 2);
    
    wait_sec(5);

    int64_t time = k_uptime_get();
    
    LOG_INF("=== L2 task 1: No Mutex Protection ===");
    LOG_INF("Expected final value: %d", INCREMENTS * 2);

    /* Wait for both workers to complete */
    k_sem_take(&done_sem, K_FOREVER);
    k_sem_take(&done_sem, K_FOREVER);

    LOG_INF("Actual final value: %u", counter);

    if (counter == INCREMENTS * 2) {
        LOG_WRN("No race this run");
    } else {
        LOG_ERR("Race condition confirmed: lost %d updates",
                (INCREMENTS * 2) - counter);
    }
    LOG_INF("Execution time: %lld ms", k_uptime_delta(&time));

    counter = 0;

    LOG_INF("=== L2 task 1: Using Mutex Protection ===");
    LOG_INF("Expected final value: %d", INCREMENTS * 2);

    wait_sec(1);

    /* Wait for both workers to complete */
    k_sem_take(&done_sem, K_FOREVER);
    k_sem_take(&done_sem, K_FOREVER);

    LOG_INF("Actual final value: %u", counter);

    if (counter == INCREMENTS * 2) {
        LOG_WRN("No race this run");
    } else {
        LOG_ERR("Race condition confirmed: lost %d updates",
                (INCREMENTS * 2) - counter);
    }
    LOG_INF("Execution time: %lld ms", k_uptime_delta(&time));

    return 0;
}
