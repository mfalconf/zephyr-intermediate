/*
 * temp_alarm - Error-path unit tests (homework skeleton)
 *
 * Task 2: fill in the stubs according to TEST_SPEC.md. Each stub calls
 * ztest_test_skip(), so the binary builds and runs cleanly until the test is
 * implemented.
 *
 * Run:
 *   west twister -T tests/temp_alarm -p native_sim
 */

#include <errno.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "sensor_fake.h"
#include "temp_alarm.h"

#define DEFAULT_THRESHOLD 30

/* Same before() hook as in test_basic.c: a clean fake and an initialised module. */
static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	sensor_fake_reset();
	sensor_fake_init();

	zassume_ok(temp_alarm_init(&fake_sensor_dev, DEFAULT_THRESHOLD),
		   "precondition: temp_alarm_init must succeed");
}

/*
 * ============================================================================
 * Test Suite: temp_alarm_sensor_errors
 *
 * Failures from the sensor driver, injected through the fakes.
 * ============================================================================
 */
ZTEST_SUITE(temp_alarm_sensor_errors, NULL, NULL, before, NULL, NULL);

ZTEST(temp_alarm_sensor_errors, test_fetch_error_propagates)
{
	/* TODO(l8-task2): a failed fetch is reported,
	 * and the module does not go on to read the channel.
	 * See TEST_SPEC.md "Suite temp_alarm_sensor_errors" #1.
	 */
	struct temp_alarm_status status;

	fake_sensor_sample_fetch_fake.return_val = -EIO;

	int ret = temp_alarm_check();

	zassert_equal(ret, -EIO, "check() must return -EIO, got %d", ret);
	zassert_equal(fake_sensor_channel_get_fake.call_count, 0U,
		      "channel_get must not be called when fetch fails");

	zassert_ok(temp_alarm_get_status(&status), "get_status() must succeed");
	zassert_equal(status.last_error, -EIO, "last_error must be -EIO, got %d", status.last_error);
}

ZTEST(temp_alarm_sensor_errors, test_channel_get_error_propagates)
{
	/* TODO(l8-task2): a failed channel read is reported.
	 * See TEST_SPEC.md "Suite temp_alarm_sensor_errors" #2.
	 */
	struct temp_alarm_status status;

	fake_sensor_channel_get_fake.custom_fake = NULL;
	fake_sensor_channel_get_fake.return_val = -EIO;

	int ret = temp_alarm_check();

	zassert_equal(ret, -EIO, "check() must return -EIO, got %d", ret);
	zassert_equal(fake_sensor_sample_fetch_fake.call_count, 1U,
		      "sample_fetch must be called once, got %u", fake_sensor_sample_fetch_fake.call_count);

	zassert_ok(temp_alarm_get_status(&status), "get_status() must succeed");
	zassert_equal(status.last_error, -EIO, "last_error must be -EIO, got %d", status.last_error);
}

ZTEST(temp_alarm_sensor_errors, test_fail_then_recover)
{
	/* TODO(l8-task2): the first fetch fails and the second one succeeds.
	 * The module must recover on the second check.
	 * See TEST_SPEC.md "Suite temp_alarm_sensor_errors" #3.
	 */
	struct temp_alarm_status status;
	static int fetch_seq[] = {-EIO, 0};

	SET_RETURN_SEQ(fake_sensor_sample_fetch, fetch_seq, ARRAY_SIZE(fetch_seq));
	sensor_fake_set_temperature(20);

	zassert_equal(temp_alarm_check(), -EIO, "first check() must fail with -EIO");
	zassert_ok(temp_alarm_check(), "second check() must succeed");

	zassert_ok(temp_alarm_get_status(&status), "get_status() must succeed");
	zassert_equal(status.last_temp, 20, "last_temp must be updated to 20, got %d", status.last_temp);
	zassert_false(status.is_alarming, "is_alarming must be false below threshold");
	zassert_equal(status.last_error, -EIO, "last_error must remain -EIO until reset, got %d",
		      status.last_error);
}

/*
 * ============================================================================
 * Test Suite: temp_alarm_invalid_input
 *
 * Arguments the public API must reject.
 * ============================================================================
 */
ZTEST_SUITE(temp_alarm_invalid_input, NULL, NULL, before, NULL, NULL);

ZTEST(temp_alarm_invalid_input, test_get_status_null)
{
	/* TODO(l8-task2): a NULL status pointer is rejected.
	 * See TEST_SPEC.md "Suite temp_alarm_invalid_input" #1.
	 */
	int ret = temp_alarm_get_status(NULL);

	zassert_equal(ret, -EINVAL, "get_status(NULL) must return -EINVAL, got %d", ret);
}

ZTEST(temp_alarm_invalid_input, test_init_without_device)
{
	/* TODO(l8-task2): init without a sensor device is rejected.
	 * See TEST_SPEC.md "Suite temp_alarm_invalid_input" #2.
	 */
	int ret = temp_alarm_init(NULL, DEFAULT_THRESHOLD);

	zassert_equal(ret, -ENODEV, "init(NULL, ...) must return -ENODEV, got %d", ret);
}

ZTEST(temp_alarm_invalid_input, test_init_threshold_out_of_range)
{
	/* TODO(l8-task2): init rejects thresholds outside the allowed range.
	 * See TEST_SPEC.md "Suite temp_alarm_invalid_input" #3.
	 */
	zassert_equal(temp_alarm_init(&fake_sensor_dev, -1), -EINVAL,
		      "init with threshold -1 must return -EINVAL");
	zassert_equal(temp_alarm_init(&fake_sensor_dev, 101), -EINVAL,
		      "init with threshold 101 must return -EINVAL");
}

ZTEST(temp_alarm_invalid_input, test_set_threshold_out_of_range)
{
	/* TODO(l8-task2): set_threshold rejects values outside the allowed range,
	 * and keeps the old threshold.
	 * See TEST_SPEC.md "Suite temp_alarm_invalid_input" #4.
	 */
	struct temp_alarm_status status;

	zassert_equal(temp_alarm_set_threshold(-1), -EINVAL, "set_threshold(-1) must return -EINVAL");
	zassert_equal(temp_alarm_set_threshold(101), -EINVAL, "set_threshold(101) must return -EINVAL");

	/* Verify that the previous threshold (30 C) is still active */
	sensor_fake_set_temperature(35);
	zassert_ok(temp_alarm_check(), "check() must succeed");

	zassert_ok(temp_alarm_get_status(&status), "get_status() must succeed");
	zassert_true(status.is_alarming, "35 C must trigger alarm because 30 C threshold was kept");
}
