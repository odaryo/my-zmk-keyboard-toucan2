/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Edge-scroll input processor.
 *
 * 一部のチェーンにしか入れないこと(ラッチを持つため)は避ける。ZMK の input listener
 * はレイヤ override が一致するとベースのチェーンを実行しない(process-next 未指定時)
 * ので、入っていないチェーンで指を離すと BTN_TOUCH の解放を取りこぼしてラッチが残る。
 */

#define DT_DRV_COMPAT zmk_input_processor_edge_scroll

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/logging/log.h>

#include <drivers/input_processor.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct es_config {
    uint16_t position_code;
    uint16_t move_code;
    uint16_t scroll_code;
    int32_t edge_min;
    int32_t edge_max;
    bool invert;
};

struct es_data {
    int32_t position;
    bool have_position;
    bool active;
};

static int es_handle_event(const struct device *dev, struct input_event *event, uint32_t param1,
                           uint32_t param2, struct zmk_input_processor_state *state) {
    const struct es_config *cfg = dev->config;
    struct es_data *data = dev->data;

    if (event->type == INPUT_EV_ABS) {
        if (event->code == cfg->position_code) {
            data->position = event->value;
            data->have_position = true;
        }
        /* そのまま流さないのは、余計なイベントを後続に渡さないため(ZMK の HID 側は
         * 絶対座標を使わない) */
        event->value = 0;
        return ZMK_INPUT_PROC_STOP;
    }

    if (event->type == INPUT_EV_KEY && event->code == INPUT_BTN_TOUCH) {
        if (event->value != 0) {
            data->active = data->have_position && data->position >= cfg->edge_min &&
                           data->position <= cfg->edge_max;
            LOG_DBG("edge scroll: touch down at %d (edge %d..%d) -> %s", data->position,
                    cfg->edge_min, cfg->edge_max, data->active ? "scroll" : "cursor");
        } else {
            data->active = false;
            /* 持ち越さないのは、次のタッチを古い位置で誤判定するため */
            data->have_position = false;
        }
        return ZMK_INPUT_PROC_CONTINUE;
    }

    /* エッジ帯で始まっていてもスクロールに変換しないのは、左ボタンを押しながらの
     * 移動がドラッグ(press-and-hold)であるため */
    if (data->active && event->type == INPUT_EV_KEY && event->code == INPUT_BTN_0 &&
        event->value != 0) {
        LOG_DBG("edge scroll: button pressed, falling back to cursor movement (drag)");
        data->active = false;
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (!data->active || event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (event->code == cfg->move_code) {
        /* ここでスケールしないのは、後続のスケーラと慣性スクロールに任せるため */
        event->code = cfg->scroll_code;
        if (cfg->invert) {
            event->value = -event->value;
        }
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (event->code == INPUT_REL_X || event->code == INPUT_REL_Y) {
        event->value = 0;
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static struct zmk_input_processor_driver_api es_driver_api = {
    .handle_event = es_handle_event,
};

#define ES_INST(n)                                                                                 \
    BUILD_ASSERT(DT_INST_PROP(n, edge_min) <= DT_INST_PROP(n, edge_max),                           \
                 "edge-min must not be greater than edge-max");                                    \
    static const struct es_config es_config_##n = {                                                \
        .position_code = DT_INST_PROP(n, position_code),                                           \
        .move_code = DT_INST_PROP(n, move_code),                                                   \
        .scroll_code = DT_INST_PROP(n, scroll_code),                                               \
        .edge_min = DT_INST_PROP(n, edge_min),                                                     \
        .edge_max = DT_INST_PROP(n, edge_max),                                                     \
        .invert = DT_INST_PROP(n, invert),                                                         \
    };                                                                                             \
    static struct es_data es_data_##n = {};                                                        \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, &es_data_##n, &es_config_##n, POST_KERNEL,                \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &es_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ES_INST)
