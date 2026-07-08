#include <stdio.h>

#include "app_init.h"
#include "gateway/gateway_main.h"
#include "soc_osal.h"

#define GATEWAY_RUNTIME_STACK_SIZE 0x6000
#define GATEWAY_RUNTIME_TASK_PRIO 24

static void *gateway_runtime_task(const char *arg)
{
    (void)arg;

    osal_msleep(500);
    printf("[GATEWAY] runtime start\r\n");
    gateway_main_init();

    while (1) {
        osal_msleep(1000);
    }

    return NULL;
}

static void gateway_runtime_entry(void)
{
    osal_task *task_handle = NULL;

    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)gateway_runtime_task,
                                      0,
                                      "gateway_task",
                                      GATEWAY_RUNTIME_STACK_SIZE);
    if (task_handle != NULL) {
        (void)osal_kthread_set_priority(task_handle, GATEWAY_RUNTIME_TASK_PRIO);
        osal_kfree(task_handle);
    }
    osal_kthread_unlock();
}

app_run(gateway_runtime_entry);
