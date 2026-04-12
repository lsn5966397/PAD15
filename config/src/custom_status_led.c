#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <stdlib.h> 

#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
// 新增：引入活动状态事件，用于休眠检测
#include <zmk/events/activity_state_changed.h> 
#include <zmk/activity.h> 

#define STRIP_NODE DT_CHOSEN(zmk_underglow)
#define NUM_PIXELS 16
#define STATUS_LED_IDX 15
#define MAX_EFFECTS 3
// 限定最大亮度
#define BRIGHTNESS_PERCENT 30 

static const struct device *led_strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[NUM_PIXELS];

static uint8_t current_layer = 0;
static uint8_t battery_level = 100;
static int status_display_frames = 0; 
static uint8_t current_effect = 0; 

// 新增：记录键盘是否处于活动状态（默认开机是活动的）
static bool is_awake = true; 

// ==========================================
// 1.  uint8_t 二维坐标表
// ==========================================
struct led_coord {
    uint8_t x;
    uint8_t y;
};

static const struct led_coord coords[NUM_PIXELS] = {
    {0, 0},  {10, 0},  {20, 0},     // 第一排
    {0, 10}, {10, 10}, {20, 10},    // 第二排
    {0, 20}, {10, 20}, {20, 20},    // 第三排
    {0, 30}, {10, 30}, {20, 30},    // 第四排
    {0, 40}, {10, 40}, {20, 40},    // 第五排
    {30, 20}                        // 第 16 颗 (状态灯)
};

static struct led_rgb wheel(uint8_t pos) {
    pos = 255 - pos;
    if (pos < 85) return (struct led_rgb){255 - pos * 3, 0, pos * 3};
    else if (pos < 170) { pos -= 85; return (struct led_rgb){0, pos * 3, 255 - pos * 3}; }
    else { pos -= 170; return (struct led_rgb){pos * 3, 255 - pos * 3, 0}; }
}

// ==========================================
// 2. 核心动画渲染线程
// ==========================================
void custom_led_thread_main(void) {
    if (!device_is_ready(led_strip)) return;

    uint32_t tick = 0; 

    while (1) {
        // 【核心新增】：如果键盘休眠了，直接全黑并大幅降低线程频率省电
        if (!is_awake) {
            for (int i = 0; i < NUM_PIXELS; i++) {
                pixels[i] = (struct led_rgb){0, 0, 0};
            }
            led_strip_update_rgb(led_strip, pixels, NUM_PIXELS);
            
            // 休眠时不需要 30ms 跑一次，500ms 检查一次足矣，极大省电
            k_sleep(K_MSEC(500)); 
            continue; // 跳过下面的所有计算，直接进入下一次循环
        }

        // --- 渲染前 15 颗矩阵灯 ---
        for (int i = 0; i < STATUS_LED_IDX; i++) {
            uint8_t cx = coords[i].x;
            uint8_t cy = coords[i].y;

            if (current_effect == 0) {
                pixels[i] = wheel((uint8_t)(tick * 2 + (cx * 2)));
            } 
            else if (current_effect == 1) {
                int scanline_y = (tick % 60); 
                int distance = abs(scanline_y - cy);
                if (distance < 12) {
                    uint8_t intensity = 255 - (distance * 20); 
                    pixels[i] = (struct led_rgb){0, intensity, intensity / 2}; 
                } else {
                    pixels[i] = (struct led_rgb){0, 0, 0}; 
                }
            }
            else {
                pixels[i] = (struct led_rgb){0, 0, 0};
            }
        }

        // --- 渲染第 16 颗状态灯 ---
        if (battery_level < 20) {
            if (tick % 30 < 15) pixels[STATUS_LED_IDX] = (struct led_rgb){0xFF, 0x00, 0x00};
            else pixels[STATUS_LED_IDX] = (struct led_rgb){0, 0, 0};
        } 
        else if (status_display_frames > 0) {
            switch (current_layer) {
                case 0: pixels[STATUS_LED_IDX] = (struct led_rgb){0x80, 0x80, 0x80}; break; 
                case 1: pixels[STATUS_LED_IDX] = (struct led_rgb){0x00, 0x00, 0xFF}; break; 
                case 2: pixels[STATUS_LED_IDX] = (struct led_rgb){0x00, 0xFF, 0x00}; break; 
                default: pixels[STATUS_LED_IDX] = (struct led_rgb){0xFF, 0x00, 0xFF}; break;
            }
            status_display_frames--; 
        } 
        else {
            pixels[STATUS_LED_IDX] = (struct led_rgb){0, 0, 0}; 
        }
        
        // 全局亮度压制
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
// 3. 事件监听器
// ==========================================

// 【核心新增】：活动状态监听器
static int activity_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev) {
        if (ev->state == ZMK_ACTIVITY_ACTIVE) {
            is_awake = true;  // 键盘被唤醒（敲击键盘时触发）
        } else {
            // ZMK_ACTIVITY_IDLE (空闲) 或 ZMK_ACTIVITY_SLEEP (深度休眠)
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
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (ev && ev->state) { 
        current_layer = ev->layer;
        status_display_frames = 66; 
    }
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
