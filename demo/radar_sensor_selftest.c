/*
 * Radar teammate local self-test entry.
 *
 * Default is OFF so the formal radar driver does not register app_run().
 * For radar-side standalone testing only, change the value below to 1,
 * build and flash. Change it back to 0 before handing code to main-control.
 */
#define RADAR_SENSOR_SELF_TEST_ENABLE 0

#if RADAR_SENSOR_SELF_TEST_ENABLE

#include "radar_sensor.h"

#include "app_init.h"
#include "common_def.h"
#include "soc_osal.h"

#define RADAR_SENSOR_SELF_TEST_STACK_SIZE 0x1000
#define RADAR_SENSOR_SELF_TEST_PRIO       25
#define RADAR_SENSOR_SELF_TEST_PERIOD_MS  1000

static int radar_sensor_selftest_task(const char *arg)
{
    unused(arg);

    osal_printk("[RADAR_SELFTEST] start\r\n");
    radar_sensor_start();

    while (1) {
        const radar_result_t *result = radar_sensor_get_result();
        osal_printk("[RADAR_SELFTEST] has_person=%d, motion_level=%d, static_time=%d, "
                    "area_id=%d, stay_time=%d, suspect_fall_hint=%d, area_stay_hint=%d\r\n",
                    result->has_person,
                    result->motion_level,
                    result->static_time,
                    result->area_id,
                    result->stay_time,
                    result->suspect_fall_hint,
                    result->area_stay_hint);
        osal_msleep(RADAR_SENSOR_SELF_TEST_PERIOD_MS);
    }

    return 0;
}

static void radar_sensor_selftest_start(void)
{
    osal_task *task = NULL;

    osal_kthread_lock();
    task = osal_kthread_create((osal_kthread_handler)radar_sensor_selftest_task, NULL,
                               "RadarSelfTest", RADAR_SENSOR_SELF_TEST_STACK_SIZE);
    if (task != NULL) {
        (void)osal_kthread_set_priority(task, RADAR_SENSOR_SELF_TEST_PRIO);
        osal_printk("[RADAR_SELFTEST] task created\r\n");
    } else {
        osal_printk("[RADAR_SELFTEST] task create failed\r\n");
    }
    osal_kthread_unlock();
}

app_run(radar_sensor_selftest_start);

#endif /* RADAR_SENSOR_SELF_TEST_ENABLE */
