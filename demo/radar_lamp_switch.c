#include "radar_lamp_switch.h"

#include <stdint.h>

#include "gpio.h"
#include "pinctrl.h"
#include "pwm.h"
#include "soc_osal.h"

#define RADAR_LAMP_GPIO                    7
#define RADAR_LAMP_SWITCH_GPIO             11
#define RADAR_LAMP_SWITCH_POLL_MS          20
#define RADAR_LAMP_SWITCH_DEBOUNCE         3
#define RADAR_LAMP_MONITOR_COUNT           250
#define RADAR_LAMP_SWITCH_STACK_SIZE       0x1000
#define RADAR_LAMP_SWITCH_TASK_PRIO        24

#define RADAR_LAMP_PWM_CHANNEL             7
#define RADAR_LAMP_PWM_PIN_MODE            1
#define RADAR_LAMP_PWM_GROUP_ID            0
#define RADAR_LAMP_PWM_PERIOD_TICKS        20000U
#define RADAR_LAMP_PERMILLE_MAX            1000U
#define RADAR_LAMP_PERCENT_SCALE           10U
#define RADAR_LAMP_SAFE_BRIGHTNESS_PERCENT 15U
#define RADAR_LAMP_FADE_DURATION_MS        3000U
#define RADAR_LAMP_FADE_STEP_MS            50U
#define RADAR_LAMP_FADE_LOG_MS             500U

typedef enum {
    RADAR_LAMP_MODE_NORMAL = 0,
    RADAR_LAMP_MODE_SAFE_LIGHTING,
    RADAR_LAMP_MODE_SAFE_EXITING
} radar_lamp_mode_t;

static bool g_radar_lamp_is_on;
static bool g_radar_lamp_task_started;
static bool g_radar_lamp_switch_stable_closed;
static bool g_pwm_output_on;
static bool g_safe_restore_switch_closed;
static uint8_t g_safe_exit_step;
static uint16_t g_output_permille;
static volatile uint8_t g_pending_safe_cmd;
static radar_lamp_mode_t g_radar_lamp_mode = RADAR_LAMP_MODE_NORMAL;

static const char *radar_lamp_switch_name(bool closed)
{
    return closed ? "CLOSED" : "OPEN";
}

static const char *radar_lamp_state_name(bool on)
{
    return on ? "ON" : "OFF";
}

static const char *radar_lamp_mode_name(radar_lamp_mode_t mode)
{
    switch (mode) {
        case RADAR_LAMP_MODE_NORMAL:
            return "NORMAL";
        case RADAR_LAMP_MODE_SAFE_LIGHTING:
            return "SAFE";
        case RADAR_LAMP_MODE_SAFE_EXITING:
            return "EXITING";
        default:
            return "UNKNOWN";
    }
}

static gpio_level_t radar_lamp_level_from_state(bool on)
{
    return on ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
}

static uint8_t radar_lamp_permille_to_percent(uint16_t permille)
{
    if (permille > RADAR_LAMP_PERMILLE_MAX) {
        permille = RADAR_LAMP_PERMILLE_MAX;
    }

    return (uint8_t)((permille + (RADAR_LAMP_PERCENT_SCALE / 2U)) / RADAR_LAMP_PERCENT_SCALE);
}

static errcode_t radar_lamp_output_init(void)
{
    errcode_t ret;

    ret = uapi_pin_set_mode(RADAR_LAMP_GPIO, HAL_PIO_FUNC_GPIO);
    if (ret != ERRCODE_SUCC) {
        osal_printk("[RADAR_LAMP] set output pin mode failed, gpio=%d ret=%d\r\n", RADAR_LAMP_GPIO, ret);
        return ret;
    }

    ret = uapi_gpio_set_dir(RADAR_LAMP_GPIO, GPIO_DIRECTION_OUTPUT);
    if (ret != ERRCODE_SUCC) {
        osal_printk("[RADAR_LAMP] set output direction failed, gpio=%d ret=%d\r\n", RADAR_LAMP_GPIO, ret);
        return ret;
    }

    ret = uapi_gpio_set_val(RADAR_LAMP_GPIO, GPIO_LEVEL_LOW);
    if (ret != ERRCODE_SUCC) {
        osal_printk("[RADAR_LAMP] set default OFF failed, gpio=%d ret=%d\r\n", RADAR_LAMP_GPIO, ret);
        return ret;
    }

    g_radar_lamp_is_on = false;
    g_output_permille = 0;
    osal_printk("[RADAR_LAMP] output init ok, lamp_gpio=%d default=OFF\r\n", RADAR_LAMP_GPIO);
    return ERRCODE_SUCC;
}

static errcode_t radar_lamp_output_drive(bool on)
{
    errcode_t ret;

    ret = uapi_gpio_set_val(RADAR_LAMP_GPIO, radar_lamp_level_from_state(on));
    if (ret != ERRCODE_SUCC) {
        osal_printk("[RADAR_LAMP] drive lamp failed, on=%d ret=%d\r\n", on, ret);
    }

    return ret;
}

static errcode_t radar_lamp_switch_input_init(void)
{
    errcode_t ret;

    ret = uapi_pin_set_mode(RADAR_LAMP_SWITCH_GPIO, HAL_PIO_FUNC_GPIO);
    if (ret != ERRCODE_SUCC) {
        osal_printk("[RADAR_LAMP] set switch pin mode failed, gpio=%d ret=%d\r\n",
            RADAR_LAMP_SWITCH_GPIO, ret);
        return ret;
    }

    ret = uapi_pin_set_pull(RADAR_LAMP_SWITCH_GPIO, PIN_PULL_TYPE_UP);
    if (ret != ERRCODE_SUCC) {
        osal_printk("[RADAR_LAMP] set switch pull-up failed, gpio=%d ret=%d\r\n",
            RADAR_LAMP_SWITCH_GPIO, ret);
        return ret;
    }

    ret = uapi_gpio_set_dir(RADAR_LAMP_SWITCH_GPIO, GPIO_DIRECTION_INPUT);
    if (ret != ERRCODE_SUCC) {
        osal_printk("[RADAR_LAMP] set switch direction failed, gpio=%d ret=%d\r\n",
            RADAR_LAMP_SWITCH_GPIO, ret);
        return ret;
    }

    osal_printk("[RADAR_LAMP] switch init ok, switch_gpio=%d active=LOW\r\n", RADAR_LAMP_SWITCH_GPIO);
    return ERRCODE_SUCC;
}

static bool radar_lamp_switch_is_closed(void)
{
    gpio_level_t level = uapi_gpio_get_val(RADAR_LAMP_SWITCH_GPIO);
    return level == GPIO_LEVEL_LOW;
}

static errcode_t radar_lamp_pwm_close_current(void)
{
    errcode_t ret = ERRCODE_SUCC;

    if (!g_pwm_output_on) {
        return ERRCODE_SUCC;
    }

#if defined(CONFIG_PWM_USING_V151)
    ret = uapi_pwm_close(RADAR_LAMP_PWM_CHANNEL);
    (void)uapi_pwm_clear_group(RADAR_LAMP_PWM_GROUP_ID);
#else
    ret = uapi_pwm_close(RADAR_LAMP_PWM_CHANNEL);
#endif
    uapi_pwm_deinit();
    g_pwm_output_on = false;

    if (ret != ERRCODE_SUCC) {
        osal_printk("[RADAR_LAMP] PWM close failed, ret=%d\r\n", ret);
    }

    return ret;
}

static errcode_t radar_lamp_stop_pwm_and_restore_gpio(void)
{
    errcode_t ret = radar_lamp_pwm_close_current();
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    return radar_lamp_output_init();
}

static void radar_lamp_pwm_build_config(uint16_t permille, pwm_config_t *cfg)
{
    uint32_t high_time;

    if (permille > RADAR_LAMP_PERMILLE_MAX) {
        permille = RADAR_LAMP_PERMILLE_MAX;
    }

    high_time = (RADAR_LAMP_PWM_PERIOD_TICKS * (uint32_t)permille) / RADAR_LAMP_PERMILLE_MAX;
    if ((permille > 0U) && (high_time == 0U)) {
        high_time = 1U;
    }
    if ((permille < RADAR_LAMP_PERMILLE_MAX) && (high_time >= RADAR_LAMP_PWM_PERIOD_TICKS)) {
        high_time = RADAR_LAMP_PWM_PERIOD_TICKS - 1U;
    }

    cfg->low_time = RADAR_LAMP_PWM_PERIOD_TICKS - high_time;
    cfg->high_time = high_time;
    cfg->offset_time = 0;
    cfg->cycles = 0;
    cfg->repeat = true;
}

static errcode_t radar_lamp_pwm_start_with_config(const pwm_config_t *cfg)
{
    errcode_t ret;

    ret = uapi_pin_set_mode(RADAR_LAMP_GPIO, RADAR_LAMP_PWM_PIN_MODE);
    if (ret != ERRCODE_SUCC) {
        osal_printk("[RADAR_LAMP] set PWM pin mode failed, gpio=%d mode=%d ret=%d\r\n",
            RADAR_LAMP_GPIO, RADAR_LAMP_PWM_PIN_MODE, ret);
        return ret;
    }

    ret = uapi_pwm_init();
    if (ret != ERRCODE_SUCC) {
        osal_printk("[RADAR_LAMP] PWM init failed, ret=%d\r\n", ret);
        return ret;
    }

    ret = uapi_pwm_open(RADAR_LAMP_PWM_CHANNEL, cfg);
    if (ret != ERRCODE_SUCC) {
        osal_printk("[RADAR_LAMP] PWM open failed, channel=%d ret=%d\r\n",
            RADAR_LAMP_PWM_CHANNEL, ret);
        uapi_pwm_deinit();
        return ret;
    }

#if defined(CONFIG_PWM_USING_V151)
    uint8_t channel_id = RADAR_LAMP_PWM_CHANNEL;
    ret = uapi_pwm_set_group(RADAR_LAMP_PWM_GROUP_ID, &channel_id, 1);
    if (ret == ERRCODE_SUCC) {
        ret = uapi_pwm_start_group(RADAR_LAMP_PWM_GROUP_ID);
    }
#else
    ret = uapi_pwm_start(RADAR_LAMP_PWM_CHANNEL);
#endif
    if (ret != ERRCODE_SUCC) {
        osal_printk("[RADAR_LAMP] PWM start failed, channel=%d ret=%d\r\n", RADAR_LAMP_PWM_CHANNEL, ret);
#if defined(CONFIG_PWM_USING_V151)
        (void)uapi_pwm_clear_group(RADAR_LAMP_PWM_GROUP_ID);
#endif
        uapi_pwm_deinit();
        return ret;
    }

    g_pwm_output_on = true;
    return ERRCODE_SUCC;
}

static errcode_t radar_lamp_pwm_reconfigure(const pwm_config_t *cfg)
{
    errcode_t ret;

#if defined(CONFIG_PWM_USING_V150)
    ret = uapi_pwm_update_duty_ratio(RADAR_LAMP_PWM_CHANNEL, cfg->low_time, cfg->high_time);
#elif defined(CONFIG_PWM_USING_V151) && defined(CONFIG_PWM_PRELOAD)
    ret = uapi_pwm_config_preload(RADAR_LAMP_PWM_GROUP_ID, RADAR_LAMP_PWM_CHANNEL, cfg);
#elif defined(CONFIG_PWM_USING_V151)
    ret = uapi_pwm_open(RADAR_LAMP_PWM_CHANNEL, cfg);
    if (ret == ERRCODE_SUCC) {
        ret = uapi_pwm_start_group(RADAR_LAMP_PWM_GROUP_ID);
    }
#else
    ret = radar_lamp_pwm_close_current();
    if (ret == ERRCODE_SUCC) {
        ret = radar_lamp_pwm_start_with_config(cfg);
    }
#endif

    if (ret != ERRCODE_SUCC) {
        osal_printk("[RADAR_LAMP] PWM reconfigure failed, ret=%d\r\n", ret);
    }

    return ret;
}

static errcode_t radar_lamp_pwm_set_permille(uint16_t permille)
{
    errcode_t ret;
    pwm_config_t cfg;

    if (permille > RADAR_LAMP_PERMILLE_MAX) {
        permille = RADAR_LAMP_PERMILLE_MAX;
    }

    radar_lamp_pwm_build_config(permille, &cfg);
    if (g_pwm_output_on) {
        ret = radar_lamp_pwm_reconfigure(&cfg);
    } else {
        ret = radar_lamp_pwm_start_with_config(&cfg);
    }
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    g_radar_lamp_is_on = (permille > 0U);
    g_output_permille = permille;
    return ERRCODE_SUCC;
}

static uint32_t radar_lamp_fade_ease(uint32_t step, uint32_t steps)
{
    uint32_t t;
    uint32_t t2;
    uint32_t t3;

    if (steps == 0U) {
        return RADAR_LAMP_PERMILLE_MAX;
    }
    if (step >= steps) {
        return RADAR_LAMP_PERMILLE_MAX;
    }

    t = (step * RADAR_LAMP_PERMILLE_MAX) / steps;
    t2 = t * t;
    t3 = (t2 * t) / RADAR_LAMP_PERMILLE_MAX;
    return ((3U * t2) - (2U * t3)) / RADAR_LAMP_PERMILLE_MAX;
}

static errcode_t radar_lamp_fade_to_permille(uint16_t target_permille)
{
    uint32_t step_ms = RADAR_LAMP_FADE_STEP_MS;
    uint32_t steps;
    uint32_t i;
    uint32_t last_log_ms = 0;
    int32_t start_permille = g_output_permille;
    int32_t diff;
    errcode_t ret;

    if (target_permille > RADAR_LAMP_PERMILLE_MAX) {
        target_permille = RADAR_LAMP_PERMILLE_MAX;
    }

    steps = RADAR_LAMP_FADE_DURATION_MS / step_ms;
    if (steps == 0U) {
        steps = 1U;
    }

    diff = (int32_t)target_permille - start_permille;
    osal_printk("[RADAR_LAMP] fade start from=%u%% to=%u%% duration=%ums\r\n",
        (unsigned int)radar_lamp_permille_to_percent((uint16_t)start_permille),
        (unsigned int)radar_lamp_permille_to_percent(target_permille),
        (unsigned int)RADAR_LAMP_FADE_DURATION_MS);

    for (i = 0; i <= steps; i++) {
        uint32_t eased = radar_lamp_fade_ease(i, steps);
        int32_t permille = start_permille + ((diff * (int32_t)eased) / (int32_t)RADAR_LAMP_PERMILLE_MAX);
        if (permille < 0) {
            permille = 0;
        } else if (permille > (int32_t)RADAR_LAMP_PERMILLE_MAX) {
            permille = RADAR_LAMP_PERMILLE_MAX;
        }

        ret = radar_lamp_pwm_set_permille((uint16_t)permille);
        if (ret != ERRCODE_SUCC) {
            return ret;
        }

        if ((i == 0U) || (i == steps) || ((i * step_ms) >= (last_log_ms + RADAR_LAMP_FADE_LOG_MS))) {
            osal_printk("[RADAR_LAMP] fade percent=%u\r\n",
                (unsigned int)radar_lamp_permille_to_percent((uint16_t)permille));
            last_log_ms = i * step_ms;
        }

        if (i < steps) {
            osal_msleep(step_ms);
        }
    }

    return ERRCODE_SUCC;
}

static errcode_t radar_lamp_set_normal(bool on)
{
    errcode_t ret;

    ret = radar_lamp_stop_pwm_and_restore_gpio();
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    ret = radar_lamp_output_drive(on);
    if (ret != ERRCODE_SUCC) {
        osal_printk("[RADAR_LAMP] set lamp failed, on=%d ret=%d\r\n", on, ret);
        return ret;
    }

    g_radar_lamp_mode = RADAR_LAMP_MODE_NORMAL;
    g_radar_lamp_is_on = on;
    g_output_permille = on ? RADAR_LAMP_PERMILLE_MAX : 0U;
    osal_printk("[RADAR_LAMP] state=%s\r\n", radar_lamp_state_name(on));
    return ERRCODE_SUCC;
}

static errcode_t radar_lamp_enter_safe_lighting(void)
{
    errcode_t ret;
    uint16_t safe_permille = RADAR_LAMP_SAFE_BRIGHTNESS_PERCENT * RADAR_LAMP_PERCENT_SCALE;

    if (safe_permille > RADAR_LAMP_PERMILLE_MAX) {
        safe_permille = RADAR_LAMP_PERMILLE_MAX;
    }

    g_safe_restore_switch_closed = g_radar_lamp_switch_stable_closed;
    g_safe_exit_step = 0;
    g_radar_lamp_mode = RADAR_LAMP_MODE_SAFE_LIGHTING;
    osal_printk("[RADAR_LAMP] safe_on source=gateway restore_switch=%s target=%u%%\r\n",
        radar_lamp_switch_name(g_safe_restore_switch_closed),
        (unsigned int)RADAR_LAMP_SAFE_BRIGHTNESS_PERCENT);

    ret = radar_lamp_fade_to_permille(safe_permille);
    if (ret != ERRCODE_SUCC) {
        osal_printk("[RADAR_LAMP] safe_on failed ret=%d\r\n", ret);
        return ret;
    }

    osal_printk("[RADAR_LAMP] safe_mode=ON brightness=%u%% exit_by_switch=%s->%s\r\n",
        (unsigned int)RADAR_LAMP_SAFE_BRIGHTNESS_PERCENT,
        radar_lamp_switch_name(!g_safe_restore_switch_closed),
        radar_lamp_switch_name(g_safe_restore_switch_closed));
    return ERRCODE_SUCC;
}

static errcode_t radar_lamp_exit_safe_lighting(const char *source, bool restore_on)
{
    errcode_t ret;
    uint16_t target_permille = restore_on ? RADAR_LAMP_PERMILLE_MAX : 0U;

    if (g_radar_lamp_mode == RADAR_LAMP_MODE_NORMAL) {
        osal_printk("[RADAR_LAMP] safe_off ignored source=%s already_normal=1\r\n", source);
        return ERRCODE_SUCC;
    }

    g_radar_lamp_mode = RADAR_LAMP_MODE_SAFE_EXITING;
    osal_printk("[RADAR_LAMP] safe_off source=%s restore_switch=%s restore_lamp=%s\r\n",
        source,
        radar_lamp_switch_name(g_radar_lamp_switch_stable_closed),
        radar_lamp_state_name(restore_on));

    ret = radar_lamp_fade_to_permille(target_permille);
    if (ret != ERRCODE_SUCC) {
        osal_printk("[RADAR_LAMP] safe_off failed ret=%d\r\n", ret);
        return ret;
    }

    return radar_lamp_set_normal(restore_on);
}

static void radar_lamp_handle_stable_switch(bool stable_closed)
{
    if (g_radar_lamp_mode == RADAR_LAMP_MODE_NORMAL) {
        (void)radar_lamp_set_normal(stable_closed);
        osal_printk("[RADAR_LAMP] switch=%s lamp=%s\r\n",
            radar_lamp_switch_name(stable_closed),
            radar_lamp_state_name(stable_closed));
        return;
    }

    if (g_radar_lamp_mode == RADAR_LAMP_MODE_SAFE_LIGHTING) {
        if ((g_safe_exit_step == 0U) && (stable_closed != g_safe_restore_switch_closed)) {
            g_safe_exit_step = 1U;
            osal_printk("[RADAR_LAMP] safe_exit_arm switch=%s need=%s\r\n",
                radar_lamp_switch_name(stable_closed),
                radar_lamp_switch_name(g_safe_restore_switch_closed));
            return;
        }

        if ((g_safe_exit_step == 1U) && (stable_closed == g_safe_restore_switch_closed)) {
            (void)radar_lamp_exit_safe_lighting("switch", stable_closed);
            return;
        }

        osal_printk("[RADAR_LAMP] safe_switch_observed switch=%s step=%u\r\n",
            radar_lamp_switch_name(stable_closed),
            (unsigned int)g_safe_exit_step);
    }
}

static void radar_lamp_handle_pending_command(void)
{
    uint8_t cmd = g_pending_safe_cmd;

    if (cmd == 0U) {
        return;
    }
    g_pending_safe_cmd = 0U;

    if (cmd == RADAR_LAMP_SAFE_CMD_ON) {
        (void)radar_lamp_enter_safe_lighting();
        return;
    }

    if (cmd == RADAR_LAMP_SAFE_CMD_OFF) {
        (void)radar_lamp_exit_safe_lighting("gateway", g_radar_lamp_switch_stable_closed);
        return;
    }

    osal_printk("[RADAR_LAMP] unknown safe command cmd=%u\r\n", (unsigned int)cmd);
}

static int radar_lamp_switch_task(const char *arg)
{
    bool stable_closed;
    bool candidate_closed;
    uint8_t same_count = 0;
    uint16_t monitor_count = 0;
    errcode_t ret;

    (void)arg;

    ret = radar_lamp_output_init();
    if (ret != ERRCODE_SUCC) {
        return -1;
    }

    ret = radar_lamp_switch_input_init();
    if (ret != ERRCODE_SUCC) {
        return -1;
    }

    stable_closed = radar_lamp_switch_is_closed();
    candidate_closed = stable_closed;
    g_radar_lamp_switch_stable_closed = stable_closed;
    (void)radar_lamp_set_normal(stable_closed);

    osal_printk("[RADAR_LAMP] initial switch=%s lamp=%s\r\n",
        radar_lamp_switch_name(stable_closed),
        radar_lamp_state_name(stable_closed));
    osal_printk("[RADAR_LAMP] switch task start, lamp_gpio=%d switch_gpio=%d active=LOW safe_pwm=%d/%d\r\n",
        RADAR_LAMP_GPIO, RADAR_LAMP_SWITCH_GPIO, RADAR_LAMP_PWM_CHANNEL, RADAR_LAMP_PWM_PIN_MODE);

    while (1) {
        bool current_closed;

        radar_lamp_handle_pending_command();
        current_closed = radar_lamp_switch_is_closed();

        if (current_closed == candidate_closed) {
            if (same_count < RADAR_LAMP_SWITCH_DEBOUNCE) {
                same_count++;
            }
        } else {
            candidate_closed = current_closed;
            same_count = 1;
        }

        if ((candidate_closed != stable_closed) && (same_count >= RADAR_LAMP_SWITCH_DEBOUNCE)) {
            stable_closed = candidate_closed;
            g_radar_lamp_switch_stable_closed = stable_closed;
            radar_lamp_handle_stable_switch(stable_closed);
            monitor_count = 0;
        }

        if (++monitor_count >= RADAR_LAMP_MONITOR_COUNT) {
            if (g_radar_lamp_mode == RADAR_LAMP_MODE_NORMAL) {
                (void)radar_lamp_output_drive(stable_closed);
            }
            osal_printk("[RADAR_LAMP] monitor switch=%s lamp=%s mode=%s percent=%u\r\n",
                radar_lamp_switch_name(stable_closed),
                radar_lamp_state_name(g_radar_lamp_is_on),
                radar_lamp_mode_name(g_radar_lamp_mode),
                (unsigned int)radar_lamp_permille_to_percent(g_output_permille));
            monitor_count = 0;
        }

        osal_msleep(RADAR_LAMP_SWITCH_POLL_MS);
    }
}

errcode_t radar_lamp_switch_start(void)
{
    osal_task *task = NULL;

    if (g_radar_lamp_task_started) {
        return ERRCODE_SUCC;
    }

    osal_kthread_lock();
    task = osal_kthread_create((osal_kthread_handler)radar_lamp_switch_task, NULL,
        "RadarLampSwitch", RADAR_LAMP_SWITCH_STACK_SIZE);
    if (task != NULL) {
        (void)osal_kthread_set_priority(task, RADAR_LAMP_SWITCH_TASK_PRIO);
        g_radar_lamp_task_started = true;
        osal_printk("[RADAR_LAMP] task created\r\n");
    } else {
        osal_printk("[RADAR_LAMP] task create failed\r\n");
    }
    osal_kthread_unlock();

    return g_radar_lamp_task_started ? ERRCODE_SUCC : ERRCODE_FAIL;
}

bool radar_lamp_switch_is_on(void)
{
    return g_radar_lamp_is_on;
}

errcode_t radar_lamp_safe_lighting_request(uint8_t cmd)
{
    if ((cmd != RADAR_LAMP_SAFE_CMD_ON) && (cmd != RADAR_LAMP_SAFE_CMD_OFF)) {
        osal_printk("[RADAR_LAMP] reject safe command cmd=%u\r\n", (unsigned int)cmd);
        return ERRCODE_FAIL;
    }

    g_pending_safe_cmd = cmd;
    osal_printk("[RADAR_LAMP] safe command queued cmd=%u\r\n", (unsigned int)cmd);
    return ERRCODE_SUCC;
}
