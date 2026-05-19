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
#define MAX_EFFECTS 4                      
#define BRIGHTNESS_PERCENT 30              

static const struct device *led_strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[NUM_PIXELS];

// ==========================================
// 状态全局变量
// ==========================================
static uint8_t battery_level = 100;
static int status_display_frames = 66;     
static uint8_t current_effect = 2;         
static uint8_t current_speed = 1;          // 【新增】动画速度倍率，开机默认 1 倍速 (最优雅缓慢)
static bool is_awake = true;
static uint8_t tracked_layer = 0;          

// ==========================================
// 工业标准 HSV 引擎 
// ==========================================
static struct led_rgb hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v) {
    struct led_rgb rgb;
    uint8_t region, remainder, p, q, t;

    if (s == 0) {
        rgb.r = v; rgb.g = v; rgb.b = v;
        return rgb;
    }

    region = h / 43; 
    remainder = (h - (region * 43)) * 6; 

    p = (v * (255 - s)) >> 8;
    q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0: rgb.r = v; rgb.g = t; rgb.b = p; break; 
        case 1: rgb.r = q; rgb.g = v; rgb.b = p; break; 
        case 2: rgb.r = p; rgb.g = v; rgb.b = t; break; 
        case 3: rgb.r = p; rgb.g = q; rgb.b = v; break; 
        case 4: rgb.r = t; rgb.g = p; rgb.b = v; break; 
        default: rgb.r = v; rgb.g = p; rgb.b = q; break; 
    }
    return rgb;
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

        // --- 1. 渲染前 15 颗矩阵灯 ---
        for (int i = 0; i < STATUS_LED_IDX; i++) {
            uint8_t col = i % 3; 
            uint8_t row = i / 3; 

            // 【关键修改】：将所有的 tick * 2 替换为 tick * current_speed，实现快捷键无级调速
            if (current_effect == 0) {
                pixels[i] = hsv_to_rgb((uint8_t)(tick * current_speed), 255, 255); 
            } 
            else if (current_effect == 1) {
                pixels[i] = hsv_to_rgb((uint8_t)(tick * current_speed - col * 30), 255, 255);
            }
            else if (current_effect == 2) {
                pixels[i] = hsv_to_rgb((uint8_t)(tick * current_speed - row * 25), 255, 255);
            }
            else if (current_effect == 3) {
                pixels[i] = hsv_to_rgb((uint8_t)(tick * current_speed - col * 20 - row * 20), 255, 255);
            }
        }

        // --- 2. 渲染第 16 颗状态灯 ---
        if (battery_level > 0 && battery_level < 10) {
            if (tick % 30 < 15) pixels[STATUS_LED_IDX] = (struct led_rgb){0xFF, 0x00, 0x00};
            else pixels[STATUS_LED_IDX] = (struct led_rgb){0, 0, 0};
        } 
        else if (status_display_frames > 0) {
            switch (tracked_layer) {
                // 【硬件级色彩校准】
                case 0: pixels[STATUS_LED_IDX] = (struct led_rgb){0xFF, 0x00, 0xA0}; break; // 修正粉色：掐断绿光，增强蓝光，完美樱花紫粉
                case 1: pixels[STATUS_LED_IDX] = (struct led_rgb){0xFF, 0xC0, 0x00}; break; // 修正橙色：大幅推高绿光，抵消红光霸权
                case 2: pixels[STATUS_LED_IDX] = (struct led_rgb){0x00, 0xFF, 0x00}; break; // 纯正绿色
                default: pixels[STATUS_LED_IDX] = (struct led_rgb){0xFF, 0xFF, 0xFF}; break; // 未知层(包括幽灵第3层)强制显示纯白，拒绝误导
            }
            status_display_frames--; 
        } 
        else {
            pixels[STATUS_LED_IDX] = (struct led_rgb){0, 0, 0}; 
        }
        
        // --- 3. 全局亮度限制 ---
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
        // 快捷键 1：切换灯效 (你在 ZMK 映射里绑定的 0x6A)
        if (ev->keycode == 0x6A) {
            current_effect++;
            if (current_effect >= MAX_EFFECTS) current_effect = 0;
        }
        // 快捷键 2：切换流动速度 【新增】
        // 你可以在 zmk 键盘映射里绑定一个不用的键 (比如对应 0x6B)，按下它就可以循环调速
        if (ev->keycode == 0x6B) {
            current_speed++;
            if (current_speed > 3) current_speed = 1; // 在 1(慢), 2(中), 3(快) 之间循环
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(keycode_status, keycode_listener);
ZMK_SUBSCRIPTION(keycode_status, zmk_keycode_state_changed);

static int layer_status_listener(const zmk_event_t *eh) {
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
