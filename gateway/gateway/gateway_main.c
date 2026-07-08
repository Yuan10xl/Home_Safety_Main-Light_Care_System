#include "gateway_radar_receiver.h"
#include "gateway_huawei_iot.h"
#include "gateway_sle_rx.h"

#include "cmsis_os2.h"

#define GATEWAY_ENABLE_HUAWEI_IOT 1
#define GATEWAY_CLOUD_START_DELAY 500
#define GATEWAY_CLOUD_TASK_STACK_SIZE 0x3000

#if GATEWAY_ENABLE_HUAWEI_IOT
static void gateway_cloud_task(void *argument)
{
    (void)argument;
    (void)osDelay(GATEWAY_CLOUD_START_DELAY);
    (void)gateway_huawei_iot_init();
}
#endif

void gateway_main_init(void)
{
    gateway_radar_receiver_init();
    (void)gateway_sle_rx_init();
#if GATEWAY_ENABLE_HUAWEI_IOT
    const osThreadAttr_t cloud_attr = {
        .name = "gateway_cloud",
        .stack_size = GATEWAY_CLOUD_TASK_STACK_SIZE,
        .priority = osPriorityNormal,
    };
    (void)osThreadNew(gateway_cloud_task, NULL, &cloud_attr);
#endif
}
