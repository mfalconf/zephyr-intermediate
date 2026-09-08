#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(l4_task1, LOG_LEVEL_DBG);

#define STACK_SIZE       2048
#define SENSOR_COUNT       10
#define SENSOR_PERIOD_MS  100

/* ================================================================== */
/*  Shared channel message                                            */
/* ================================================================== */

struct sensor_data {
    int32_t temperature_mc;
    uint32_t timestamp_ms;
    uint8_t seq;
};

/* Forward declarations required before observer/channel definitions. */
static void display_listener_cb(const struct zbus_channel *chan);

/* ================================================================== */
/*  Observers                                                         */
/* ================================================================== */

ZBUS_LISTENER_DEFINE(display_listener, display_listener_cb);

/*
 * Logger is a subscriber.
 */
ZBUS_SUBSCRIBER_DEFINE(logger_sub, 1);

/* ================================================================== */
/*  Channel                                                           */
/* ================================================================== */

ZBUS_CHAN_DEFINE(sensor_chan, struct sensor_data,
                 NULL, NULL,
                 ZBUS_OBSERVERS(display_listener, logger_sub),
                 ZBUS_MSG_INIT(.temperature_mc = 0,
                               .timestamp_ms = 0,
                               .seq = 0));

/* ================================================================== */
/*  Listener - synchronous observer                                   */
/* ================================================================== */

static void display_listener_cb(const struct zbus_channel *chan)
{
    const struct sensor_data *msg =
        (const struct sensor_data *)zbus_chan_const_msg(chan);

    /*
     * Listener runs in publisher context.
     * Keep it short. No blocking work here.
     */
    LOG_INF("[DISPLAY-LIS] thread=%s seq=%u temp=%d mC",
            k_thread_name_get(k_current_get()),
            msg->seq,
            msg->temperature_mc);
}

/* ================================================================== */
/*  Publisher                                                         */
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
        data.seq,
        data.temperature_mc);

    int ret = zbus_chan_pub(&sensor_chan, &data, K_NO_WAIT);
    if (ret != 0) {
        LOG_WRN("[SENSOR] publish failed ret=%d", ret);
        k_sem_give(&timer_done);
    }

    counter++;
    if(counter >= SENSOR_COUNT) {
        k_sem_give(&timer_done);
        LOG_INF("[SENSOR] done");
    }
}

K_TIMER_DEFINE(timer, timer_expiry, NULL);


/* ================================================================== */
/*  Subscriber - logger                                               */
/* ================================================================== */

static void logger_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    k_thread_name_set(k_current_get(), "logger");

    const struct zbus_channel *chan;
    int received = 0;

    while (received < SENSOR_COUNT) {
        struct sensor_data msg;

        int ret = zbus_sub_wait(&logger_sub, &chan, K_MSEC(1500));
        if (ret != 0) {
            LOG_WRN("[LOGGER-MSG] timeout ret=%d", ret);
            break;
        }

        zbus_chan_read(chan, &msg, K_NO_WAIT);
        received++;

        LOG_INF("[LOGGER-MSG] thread=%s seq=%u temp=%d latency=%ums",
                k_thread_name_get(k_current_get()),
                msg.seq,
                msg.temperature_mc,
                k_uptime_get_32() - msg.timestamp_ms);
    }

    LOG_INF("[LOGGER-MSG] done received=%d", received);
}

/* ================================================================== */
/*  Threads                                                           */
/* ================================================================== */

K_THREAD_DEFINE(logger_thread, STACK_SIZE, logger_thread_fn,
                NULL, NULL, NULL, 6, 0, 0);

/* ================================================================== */
/*  Main                                                              */
/* ================================================================== */

int main(void)
{
    LOG_INF("=== L4 Task 1: Zbus Sensor Pub-Sub ===");
    LOG_INF("Sensor publishes every %dms", SENSOR_PERIOD_MS);
    LOG_INF("Display listener runs in publisher context");
    LOG_INF("Logger uses standard subscriber notifications");
    
    k_timer_start(&timer, K_MSEC(SENSOR_PERIOD_MS), K_MSEC(SENSOR_PERIOD_MS));
    k_sem_take(&timer_done, K_FOREVER);
    k_timer_stop(&timer);

    return 0;
}
