/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Swipe-to-keys input processor.
 *
 * 発火のロックを軸ごとに持たないのは、斜めのスワイプで縦横のショートカットが同時に
 * 出てしまうため。対象イベントを素通しにしないのは、同じ操作がスクロールとしても
 * 流れてしまうため。
 */

#define DT_DRV_COMPAT zmk_input_processor_swipe_keys

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/logging/log.h>

#include <drivers/input_processor.h>
#include <zmk/behavior.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define SK_MAX_CODES 4

struct sk_config {
    const uint16_t *codes;
    uint8_t codes_len;
    const struct zmk_behavior_binding *bindings;
    int32_t threshold;
    uint16_t timeout_ms;
    uint16_t throttle_ms;
    bool pass_through;
};

struct sk_axis {
    int32_t accum;
    int32_t peak;
};

struct sk_data {
    struct sk_axis *axes;
    int64_t last_time;
    int64_t fire_time;
    /* 軸ごとに分けないのは、斜めスワイプで縦横が同時に発火するため */
    bool locked;
};

static inline int32_t sk_abs(int32_t v) { return v < 0 ? -v : v; }

static int sk_axis_index(const struct sk_config *cfg, uint16_t code) {
    for (int i = 0; i < cfg->codes_len; i++) {
        if (cfg->codes[i] == code) {
            return i;
        }
    }
    return -1;
}

static void sk_reset(const struct sk_config *cfg, struct sk_data *data) {
    for (int i = 0; i < cfg->codes_len; i++) {
        data->axes[i].accum = 0;
        data->axes[i].peak = 0;
    }
    data->locked = false;
}

static void sk_fire(const struct sk_config *cfg, int axis_index, bool positive) {
    const struct zmk_behavior_binding *binding =
        &cfg->bindings[axis_index * 2 + (positive ? 1 : 0)];
    struct zmk_behavior_binding_event event = {
        .position = INT32_MAX,
        .timestamp = k_uptime_get(),
    };

    zmk_behavior_invoke_binding(binding, event, true);
    zmk_behavior_invoke_binding(binding, event, false);
}

static int sk_handle_event(const struct device *dev, struct input_event *event, uint32_t param1,
                           uint32_t param2, struct zmk_input_processor_state *state) {
    const struct sk_config *cfg = dev->config;
    struct sk_data *data = dev->data;

    if (event->type == INPUT_EV_KEY && event->code == INPUT_BTN_TOUCH && event->value == 0) {
        sk_reset(cfg, data);
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    int idx = sk_axis_index(cfg, event->code);
    if (idx < 0) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    struct sk_axis *axis = &data->axes[idx];
    int64_t now = k_uptime_get();

    if (data->last_time != 0 && (now - data->last_time) > cfg->timeout_ms) {
        sk_reset(cfg, data);
    }
    data->last_time = now;

    if (data->locked && cfg->throttle_ms > 0 && (now - data->fire_time) >= cfg->throttle_ms) {
        sk_reset(cfg, data);
    }

    if (event->value != 0) {
        axis->accum += event->value;

        /* 逆符号1回でリセットしないのは、ドライバの出す値が1レポート数単位で符号も
         * ぶれるため。それではスワイプが成立しない。 */
        if (sk_abs(axis->accum) > sk_abs(axis->peak)) {
            axis->peak = axis->accum;
        } else if (sk_abs(axis->peak - axis->accum) >= cfg->threshold / 2) {
            axis->accum = event->value;
            axis->peak = event->value;
        }

        LOG_DBG("swipe keys: code %d delta %d accum %d/%d", cfg->codes[idx], event->value,
                axis->accum, cfg->threshold);
    }

    if (!data->locked && sk_abs(axis->accum) >= cfg->threshold) {
        LOG_DBG("swipe keys: code %d fired (accum %d, threshold %d)", cfg->codes[idx], axis->accum,
                cfg->threshold);
        sk_fire(cfg, idx, axis->accum > 0);
        sk_reset(cfg, data);
        data->fire_time = now;
        data->locked = true;
    }

    if (!cfg->pass_through) {
        event->value = 0;
        /* STOP だけに頼らないのは、レイヤ override のチェーン内では listener 側が
         * STOP を無視するため。値を0にするほうが実際の抑止になっている。 */
        return ZMK_INPUT_PROC_STOP;
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static struct zmk_input_processor_driver_api sk_driver_api = {
    .handle_event = sk_handle_event,
};

#define SK_BINDING_AT(idx, n)                                                                      \
    {                                                                                              \
        .behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(n, bindings, idx)),                   \
        .param1 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(n, bindings, idx, param1), (0),           \
                              (DT_INST_PHA_BY_IDX(n, bindings, idx, param1))),                      \
        .param2 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(n, bindings, idx, param2), (0),           \
                              (DT_INST_PHA_BY_IDX(n, bindings, idx, param2))),                      \
    }

#define SK_INST(n)                                                                                 \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, codes) <= SK_MAX_CODES,                                       \
                 "swipe-keys supports at most 4 codes");                                           \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, bindings) == DT_INST_PROP_LEN(n, codes) * 2,                  \
                 "swipe-keys requires 2 bindings (negative, positive) per code");                  \
    static const uint16_t sk_codes_##n[] = DT_INST_PROP(n, codes);                                 \
    static const struct zmk_behavior_binding sk_bindings_##n[] = {                                 \
        LISTIFY(DT_INST_PROP_LEN(n, bindings), SK_BINDING_AT, (, ), n)};                            \
    static struct sk_axis sk_axes_##n[DT_INST_PROP_LEN(n, codes)];                                 \
    static const struct sk_config sk_config_##n = {                                                \
        .codes = sk_codes_##n,                                                                     \
        .codes_len = DT_INST_PROP_LEN(n, codes),                                                   \
        .bindings = sk_bindings_##n,                                                               \
        .threshold = DT_INST_PROP(n, threshold),                                                   \
        .timeout_ms = DT_INST_PROP(n, timeout_ms),                                                 \
        .throttle_ms = DT_INST_PROP(n, throttle_ms),                                               \
        .pass_through = DT_INST_PROP(n, pass_through),                                              \
    };                                                                                             \
    static struct sk_data sk_data_##n = {                                                          \
        .axes = sk_axes_##n,                                                                       \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, &sk_data_##n, &sk_config_##n, POST_KERNEL,                \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &sk_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SK_INST)
