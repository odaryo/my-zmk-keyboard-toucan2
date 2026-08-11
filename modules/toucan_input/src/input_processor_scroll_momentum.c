/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Two-stage scroll speed + momentum (inertia) input processor.
 *
 * 慣性中のイベントをトラックパッドのデバイスに対して送出しないのは、チェーンを
 * 再通過して再スケール・再トリガされるため。専用の zmk,input-listener を
 * このプロセッサ自身のデバイスに紐付けて HID まで届ける。
 */

#define DT_DRV_COMPAT zmk_input_processor_scroll_momentum

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/logging/log.h>

#include <drivers/input_processor.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define SM_SAMPLES 16
#define SM_MAX_CODES 4

struct sm_sample {
    int64_t time;
    int16_t value;
};

struct sm_axis {
    struct sm_sample samples[SM_SAMPLES];
    uint8_t next;
    int64_t last_time;
    int32_t velocity;
    bool fast;
    int32_t coast_speed;
    int32_t coast_rem;
};

struct sm_config {
    const uint16_t *codes;
    uint8_t codes_len;
    uint16_t fast_threshold;
    uint16_t fast_gain;
    uint16_t coast_gain;
    uint16_t max_coast_speed;
    uint16_t decay;
    uint16_t tick_ms;
    uint16_t release_ms;
    uint16_t window_ms;
    uint16_t max_duration_ms;
    bool stop_on_move;
};

struct sm_data {
    const struct device *dev;
    struct k_work_delayable coast_work;
    struct sm_axis *axes;
    bool coasting;
    int64_t coast_start;
};

static inline int32_t sm_abs(int32_t v) { return v < 0 ? -v : v; }

static int sm_axis_index(const struct sm_config *cfg, uint16_t code) {
    for (int i = 0; i < cfg->codes_len; i++) {
        if (cfg->codes[i] == code) {
            return i;
        }
    }
    return -1;
}

/*
 * 実測期間 (now - 最古サンプル) で割らないのは、イベントが1個しか入っていないときに
 * 「1単位を極短時間で動いた」と解釈され、ゆっくりスクロールでも fast 判定になるため。
 * 固定窓ではスワイプ開始直後が過小評価になるが、持続的に速い動きだけを拾いたい。
 */
static int32_t sm_velocity(const struct sm_config *cfg, const struct sm_axis *axis, int64_t now) {
    int32_t sum = 0;

    for (int i = 0; i < SM_SAMPLES; i++) {
        const struct sm_sample *s = &axis->samples[i];
        if (s->time == 0 || (now - s->time) > cfg->window_ms) {
            continue;
        }
        sum += s->value;
    }

    return (sum * 1000) / (int32_t)cfg->window_ms;
}

static void sm_reset_axis_measurement(struct sm_axis *axis) {
    for (int i = 0; i < SM_SAMPLES; i++) {
        axis->samples[i].time = 0;
        axis->samples[i].value = 0;
    }
    axis->next = 0;
    axis->velocity = 0;
    axis->fast = false;
}

static void sm_stop_coasting(const struct device *dev) {
    const struct sm_config *cfg = dev->config;
    struct sm_data *data = dev->data;

    data->coasting = false;
    for (int i = 0; i < cfg->codes_len; i++) {
        data->axes[i].coast_speed = 0;
        data->axes[i].coast_rem = 0;
    }
}

static void sm_cancel(const struct device *dev) {
    struct sm_data *data = dev->data;

    sm_stop_coasting(dev);
    k_work_cancel_delayable(&data->coast_work);
}

static bool sm_start_coasting(const struct device *dev, int64_t now) {
    const struct sm_config *cfg = dev->config;
    struct sm_data *data = dev->data;
    bool any = false;

    for (int i = 0; i < cfg->codes_len; i++) {
        struct sm_axis *axis = &data->axes[i];
        int32_t mag = sm_abs(axis->velocity);
        bool recent = axis->last_time != 0 &&
                      (now - axis->last_time) <= (int64_t)cfg->release_ms + 2 * cfg->tick_ms;

        if (axis->fast && recent && mag >= cfg->fast_threshold) {
            /* 上限を設けないのは、速すぎるフリックで慣性が伸びすぎるため */
            int32_t start = MIN(mag, (int32_t)cfg->max_coast_speed);
            if (axis->velocity < 0) {
                start = -start;
            }
            axis->coast_speed = start * (int32_t)cfg->coast_gain;
            axis->coast_rem = 0;
            any = true;
            LOG_DBG("scroll momentum start: code %d velocity %d units/s (coast %d units/s)",
                    cfg->codes[i], axis->velocity, start * cfg->coast_gain / 1000);
        } else {
            axis->coast_speed = 0;
            axis->coast_rem = 0;
        }
    }

    return any;
}

static void sm_coast_work_cb(struct k_work *work) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(work);
    struct sm_data *data = CONTAINER_OF(d_work, struct sm_data, coast_work);
    const struct device *dev = data->dev;
    const struct sm_config *cfg = dev->config;
    int64_t now = k_uptime_get();

    if (!data->coasting) {
        if (!sm_start_coasting(dev, now)) {
            return;
        }
        data->coasting = true;
        data->coast_start = now;
    }

    int16_t out[SM_MAX_CODES] = {0};
    int last = -1;

    for (int i = 0; i < cfg->codes_len; i++) {
        struct sm_axis *axis = &data->axes[i];
        if (axis->coast_speed == 0) {
            continue;
        }

        int32_t total = axis->coast_rem + (axis->coast_speed * (int32_t)cfg->tick_ms) / 1000;
        int32_t units = total / 1000;
        axis->coast_rem = total - units * 1000;

        if (units != 0) {
            out[i] = (int16_t)CLAMP(units, INT16_MIN, INT16_MAX);
            last = i;
        }
    }

    for (int i = 0; i < cfg->codes_len; i++) {
        if (out[i] != 0) {
            input_report_rel(dev, cfg->codes[i], out[i], i == last, K_NO_WAIT);
        }
    }

    bool active = false;
    for (int i = 0; i < cfg->codes_len; i++) {
        struct sm_axis *axis = &data->axes[i];
        if (axis->coast_speed == 0) {
            continue;
        }
        axis->coast_speed = (axis->coast_speed * (int32_t)cfg->decay) / 1000;
        if (sm_abs(axis->coast_speed) < 1000) {
            axis->coast_speed = 0;
            axis->coast_rem = 0;
        } else {
            active = true;
        }
    }

    if (active && (now - data->coast_start) < cfg->max_duration_ms) {
        k_work_reschedule(&data->coast_work, K_MSEC(cfg->tick_ms));
    } else {
        sm_stop_coasting(dev);
        LOG_DBG("scroll momentum end");
    }
}

static int sm_handle_event(const struct device *dev, struct input_event *event, uint32_t param1,
                           uint32_t param2, struct zmk_input_processor_state *state) {
    const struct sm_config *cfg = dev->config;
    struct sm_data *data = dev->data;

    if (event->type == INPUT_EV_REL) {
        int idx = sm_axis_index(cfg, event->code);

        if (idx >= 0) {
            int64_t now = k_uptime_get();
            struct sm_axis *axis = &data->axes[idx];

            /* 慣性を続けないのは、ユーザーの新しい操作を優先するため */
            if (data->coasting) {
                sm_stop_coasting(dev);
            }

            if (axis->last_time != 0 && (now - axis->last_time) > cfg->release_ms) {
                sm_reset_axis_measurement(axis);
            }

            axis->samples[axis->next].time = (now != 0) ? now : 1;
            axis->samples[axis->next].value = event->value;
            axis->next = (axis->next + 1) % SM_SAMPLES;
            axis->last_time = now;

            axis->velocity = sm_velocity(cfg, axis, now);

            int32_t mag = sm_abs(axis->velocity);
            if (mag >= cfg->fast_threshold) {
                axis->fast = true;
            } else if (mag < (int32_t)cfg->fast_threshold * 3 / 4) {
                axis->fast = false;
            }

            if (axis->fast && event->value != 0) {
                int32_t scaled_num = (int32_t)event->value * (int32_t)cfg->fast_gain;
                if (state && state->remainder) {
                    scaled_num += *state->remainder;
                }
                int32_t scaled = scaled_num / 1000;
                if (state && state->remainder) {
                    *state->remainder = (int16_t)(scaled_num - scaled * 1000);
                }
                event->value = (int16_t)CLAMP(scaled, INT16_MIN, INT16_MAX);
            }

            k_work_reschedule(&data->coast_work, K_MSEC(cfg->release_ms));

            return ZMK_INPUT_PROC_CONTINUE;
        }

        if (cfg->stop_on_move && event->value != 0 &&
            (event->code == INPUT_REL_X || event->code == INPUT_REL_Y)) {
            sm_cancel(dev);
        }
    } else if (event->type == INPUT_EV_KEY && event->value > 0) {
        sm_cancel(dev);
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static int sm_init(const struct device *dev) {
    struct sm_data *data = dev->data;

    data->dev = dev;
    k_work_init_delayable(&data->coast_work, sm_coast_work_cb);

    return 0;
}

static struct zmk_input_processor_driver_api sm_driver_api = {
    .handle_event = sm_handle_event,
};

#define SM_INST(n)                                                                                 \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, codes) <= SM_MAX_CODES,                                       \
                 "scroll-momentum supports at most 4 codes");                                      \
    static const uint16_t sm_codes_##n[] = DT_INST_PROP(n, codes);                                 \
    static struct sm_axis sm_axes_##n[DT_INST_PROP_LEN(n, codes)];                                 \
    static const struct sm_config sm_config_##n = {                                                \
        .codes = sm_codes_##n,                                                                     \
        .codes_len = DT_INST_PROP_LEN(n, codes),                                                   \
        .fast_threshold = DT_INST_PROP(n, fast_threshold),                                         \
        .fast_gain = DT_INST_PROP(n, fast_gain),                                                   \
        .coast_gain = DT_INST_PROP(n, coast_gain),                                                 \
        .max_coast_speed = DT_INST_PROP(n, max_coast_speed),                                       \
        .decay = DT_INST_PROP(n, decay),                                                        \
        .tick_ms = DT_INST_PROP(n, tick_ms),                                                       \
        .release_ms = DT_INST_PROP(n, release_ms),                                                 \
        .window_ms = DT_INST_PROP(n, window_ms),                                                   \
        .max_duration_ms = DT_INST_PROP(n, max_duration_ms),                                       \
        .stop_on_move = DT_INST_PROP(n, stop_on_move),                                             \
    };                                                                                             \
    static struct sm_data sm_data_##n = {                                                          \
        .axes = sm_axes_##n,                                                                       \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, sm_init, NULL, &sm_data_##n, &sm_config_##n, POST_KERNEL,              \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &sm_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SM_INST)
