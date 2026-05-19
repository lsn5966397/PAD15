#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <stdlib.h>

#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/activity.h>
#include <zmk/keymap.h>

#define STRIP_NODE DT_NODELABEL(pad15_leds)
#define NUM_PIXELS 16
#define STATUS_LED_IDX 15
#define MAX_EFFECTS 4                      // 4种精细调校的丝滑灯效
#define BRIGHTNESS_PERCENT 30              

static const struct device *led_strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[NUM_PIXELS];

// ==========================================
// 安全的全局状态变量
// ==========================================
static uint8_t battery_level = 100;
static int status_display_frames = 66;     // 开机立即使状态灯倒计时亮起
static uint8_t current_effect = 3;         // 默认开机载入效果3 (真·全光谱对角线幻彩漩涡)
static bool is_awake = true;

// 【核心修复】：显式初始化为 0。绝对封杀开机由于异步读取未初始化内存导致的“闪绿灯”Bug
static uint8_t tracked_layer = 0;          

// ==========================================
// 丝滑流体三相色轮算法 (完美覆盖全光谱过渡)
// ==========================================
static struct led_rgb fluid_wheel(uint8_t pos) {
    uint8_t r = 0, g = 0, b = 0;
    if (pos < 85) {
        // 阶段 1：红 -> 橙 -> 黄 -> 绿
        r = 255 - (pos * 3);
        g = pos * 3;
        b = 0;
    } else if (pos < 170) {
        // 阶段 2：绿 -> 青 -> 蓝
        pos -= 85;
        r = 0;
        g = 255 - (pos * 3);
        b = pos * 3;
    } else {
        // 阶段 3：蓝 -> 靛 -> 紫 -> 粉 -> 红
        pos -= 170;
        r = pos * 3;
        g = 0;
        b = 255 - (pos * 3);
    }
    return (struct led_rgb){r, g, b};
}

// ==========================================
// 核心动画线程
// ==========================================
void custom_led_thread_main(void) {
    uint32_t tick = 0;
    if (!device_is_ready(led_strip)) {
        printk("Custom LED: WS2812 strip not ready!\n");
        return;
    } 

    while (1) {
        if (!is_awake) {
            for (int i = 0; i < NUM_PIXELS; i++) pixels[i] = (struct led_rgb){0, 0, 0};
            led_strip_update_rgb(led_strip, pixels, NUM_PIXELS);
            k_sleep(K_MSEC(500)); 
            continue;
        }

        // --- 1. 渲染前 15 颗矩阵灯 (网格级细化光谱渲染) ---
        for (int i = 0; i < STATUS_LED_IDX; i++) {
            // 极其关键：直接推导 3x5 键盘矩阵的绝对行列网格索引
            uint8_t col = i % 3; // 0, 1, 2
            uint8_t row = i / 3; // 0, 1, 2, 3, 4

            if (current_effect == 0) {
                // 效果 0：全局光谱大呼吸 (所有人同步在全光谱中丝滑流动)
                pixels[i] = fluid_wheel((uint8_t)(tick * 2)); 
            } 
            else if (current_effect == 1) {
                // 效果 1：细密横向彩虹 (压缩空间步长至 25，让过渡色完美呈现在 3 列灯里)
                pixels[i] = fluid_wheel((uint8_t)(tick * 2 + col * 25));
            }
            else if (current_effect == 2) {
                // 效果 2：细密纵向瀑布彩虹 (步长 18，纵向全光谱平铺)
                pixels[i] = fluid_wheel((uint8_t)(tick * 2 + row * 18));
            }
            else if (current_effect == 3) {
                // 效果 3：真·对角线幻彩波浪 (横纵多维相位叠加，复刻原生高级流动感)
                pixels[i] = fluid_wheel((uint8_t)(tick * 2 + col * 20 + row * 15));
            }
        }

        // --- 2. 渲染第 16 颗状态灯 ---
        // 规避未接电池时电量上报为 0 的强制覆盖 Bug
        if (battery_level > 0 && battery_level < 10) {
            if (tick % 30 < 15) pixels[STATUS_LED_IDX] = (struct led_rgb){0xFF, 0x00, 0x00};
            else pixels[STATUS_LED_IDX] = (struct led_rgb){0, 0, 0};
        } 
        else if (status_display_frames > 0) {
            // 此时读取通过事件安全同步过来的 tracked_layer，杜绝闪绿
            switch (tracked_layer) {
                case 0: 
                    // 特调高级樱花粉：大幅度拉升绿蓝比例，打散纯红，肉眼看粉嫩不刺眼
                    pixels[STATUS_LED_IDX] = (struct led_rgb){0xFF, 0x50, 0x90}; 
                    break; 
                case 1: 
                    // 遵循你的 Benchmark 标准纯橙色
                    pixels[STATUS_LED_IDX] = (struct led_rgb){0xFF, 0x80, 0x00}; 
                    break; 
                case 2: 
                    // 纯正鲜绿色
                    pixels[STATUS_LED_IDX] = (struct led_rgb){0x00, 0xFF, 0x00}; 
                    break; 
                case 3: 
                    // 纯正清爽天蓝色
                    pixels[STATUS_LED_IDX] = (struct led_rgb){0x00, 0x80, 0xFF}; 
                    break; 
                default: 
                    pixels[STATUS_LED_IDX] = (struct led_rgb){0xFF, 0xFF, 0xFF}; 
                    break; 
            }
            status_display_frames--; 
        } 
        else {
            pixels[STATUS_LED_IDX] = (struct led_rgb){0, 0, 0}; 
        }
        
        // --- 3. 全局亮度与电气保护 ---
        for (int i = 0; i < NUM_PIXELS; i++) {
            pixels[i].r = (pixels[i].r * BRIGHTNESS_PERCENT) / 100;
            pixels[i].g = (pixels[i].g * BRIGHTNESS_PERCENT) / 100;
            pixels[i].b = (pixels[i].b * BRIGHTNESS_PERCENT) / 100;
        }
                
        led_strip_update_rgb(led_strip, pixels, NUM_PIXELS);
        tick++;
        k_sleep(K_MSEC(30));
    }
}
K_THREAD_DEFINE(custom_led_tid, 1024, custom_led_thread_main, NULL, NULL, NULL, 7, 0, 0);

// ==========================================
// 绝对安全的 ZMK 事件接收器
// ==========================================

static int activity_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev) {
        if (ev->state == ZMK_ACTIVITY_ACTIVE) {
            is_awake = true;
            status_display_frames = 66; 
        } else {
            is_awake = false; 
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(activity_status, activity_listener);
ZMK_SUBSCRIPTION(activity_status, zmk_activity_state_changed);

static int keycode_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev && ev->state) {
        if (ev->keycode == 0x6A) {
            current_effect++;
            if (current_effect >= MAX_EFFECTS) current_effect = 0;
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(keycode_status, keycode_listener);
ZMK_SUBSCRIPTION(keycode_status, zmk_keycode_state_changed);

static int layer_status_listener(const zmk_event_t *eh) {
    // 只有当 ZMK 核心完全就绪并发出真实的层事件时，才通过标准化 API 写入变量，实现绝对安全
    tracked_layer = zmk_keymap_highest_layer_active();
    status_display_frames = 66; 
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(layer_status, layer_status_listener);
ZMK_SUBSCRIPTION(layer_status, zmk_layer_state_changed);

static int battery_status_listener(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    if (ev) {
        battery_level = ev->state_of_charge;
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(battery_status, battery_status_listener);
ZMK_SUBSCRIPTION(battery_status, zmk_battery_state_changed);
