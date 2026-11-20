# Response-Time Guarantee & Priority Analysis

## Response-Time Guarantee

We measure the start latency (dispatch delay), not total execution time. The target is under 10 ms (DEADLINE_MS).

* Start: ready_ms is saved in event_timer_expiry() when the event goes into control_queue (logs event_ready).
* End: Measured in control after k_msgq_get(), doing latency_ms = k_uptime_get_32() - event.ready_ms at main.c:89 (logs event_done).

## Root Cause (FIX_CONTROL_PRIORITY = 0)

In Zephyr, lower number means higher priority. Since MAINTENANCE_PRIORITY is 4 and CONTROL_PRIORITY is 7, maintenance runs first.

In babeltrace.log:
* 00:00:00.500: event_ready happens and both threads wake up.
* maintenance (priority 4) takes the CPU and runs k_busy_wait(45000) for 45 ms.
* 00:00:00.545: maintenance finishes, so control finally runs and logs event_done with arg1 = 40 (over the 10 ms limit).

### Observation

It shows 40 ms instead of 45 ms because k_uptime_get_32() uses 10 ms system ticks (CONFIG_SYS_CLOCK_TICKS_PER_SEC=100), giving 540 - 500 = 40 ms.

## Fix (FIX_CONTROL_PRIORITY = 1)

Setting CONTROL_PRIORITY to 3 (3 < 4) gives control higher priority. Now control runs right after the ISR and finishes inside the 10 ms deadline (arg1 = 0) before maintenance does its 45 ms work.