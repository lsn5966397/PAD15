#include <zephyr/kernel.h>                 // 引入 Zephyr 操作系统内核 API (提供多线程、延时等功能)
#include <zephyr/device.h>                 // 引入设备驱动模型 API
#include <zephyr/drivers/led_strip.h>      // 引入 LED 灯带的标准驱动接口
#include <stdlib.h>                        // 引入 C 标准库 (提供绝对值 abs() 等数学函数)

#include <zmk/event_manager.h>             // 引入 ZMK 的事件管理器 (用于监听键盘状态)
#include <zmk/events/layer_state_changed.h>// 引入键盘层 (Layer) 切换事件
#include <zmk/events/battery_state_changed.h>// 引入电池电量变化事件
#include <zmk/events/keycode_state_changed.h>// 引入按键按下/抬起事件
#include <zmk/events/activity_state_changed.h>// 引入键盘活动状态 (活跃/休眠) 事件
#include <zmk/activity.h>                  // 引入活动状态相关的定义
#include <zmk/keymap.h>                    // ZMK 核心键盘层 API，获取准确的层状态

#define STRIP_NODE DT_NODELABEL(pad15_leds)// 通过设备树标签 (pad15_leds) 获取硬件节点宏
#define NUM_PIXELS 16                      // 定义键盘上总共的灯珠数量 (15个矩阵灯 + 1个状态灯)
#define STATUS_LED_IDX 15                  // 定义状态灯在数组中的索引位置 (由于从0开始，第16颗就是索引15)
#define MAX_EFFECTS 2                      // 定义最大灯效数量 (0: 横向波浪, 1: 纵向波浪)
#define BRIGHTNESS_PERCENT 30              // 定义全局最高亮度百分比 (30%)，防止刺眼和耗电过大

static const struct device *led_strip = DEVICE_DT_GET(STRIP_NODE); // 在初始化时获取 WS2812 的设备句柄
static struct led_rgb pixels[NUM_PIXELS];  // 定义一个 RGB 结构体数组，用来暂存每一颗灯珠的颜色数据

// 状态全局变量
static uint8_t current_layer = 0;          // 记录当前处于键盘的第几层 (默认第 0 层)
static uint8_t battery_level = 100;        // 记录当前的电池电量 (默认 100%)
static int status_display_frames = 66;      // 记录状态灯应该亮起的持续帧数 (用于切换层时短暂显示颜色)
static uint8_t current_effect = 0;         // 记录当前的灯光效果模式 (默认效果 0)
static bool is_awake = true;               // 记录键盘是否处于唤醒状态 (默认开机是唤醒的)
        
struct led_coord {
    uint8_t x;
    uint8_t y;
};

static const struct led_coord coords[NUM_PIXELS] = {
    {0, 0},  {10, 0},  {20, 0},
    {0, 10}, {10, 10}, {20, 10},
    {0, 20}, {10, 20}, {20, 20},
    {0, 30}, {10, 30}, {20, 30},
    {0, 40}, {10, 40}, {20, 40},
    {30, 20}
};

// ==========================================
// 完美无缝的 HSV 彩虹色轮函数 (赤橙黄绿青蓝紫)
// ==========================================
static struct led_rgb wheel(uint8_t pos) {
    if (pos < 85) {
        return (struct led_rgb){255 - pos * 3, pos * 3, 0}; 
    } else if (pos < 170) {
        pos -= 85; 
        return (struct led_rgb){0, 255 - pos * 3, pos * 3};
    } else {
        pos -= 170; 
        return (struct led_rgb){pos * 3, 0, 255 - pos * 3};
    }
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
            for (int i = 0; i < NUM_PIXELS; i++) {
                pixels[i] = (struct led_rgb){0, 0, 0};
            }
            led_strip_update_rgb(led_strip, pixels, NUM_PIXELS);
            k_sleep(K_MSEC(500)); 
            continue;
        }

        // --- 1. 渲染前 15 颗矩阵灯 ---
        for (int i = 0; i < STATUS_LED_IDX; i++) {
            uint8_t cx = coords[i].x;
            uint8_t cy = coords[i].y;

            if (current_effect == 0) {
                // 效果 0：全局呼吸渐变 (所有灯同一个颜色，按赤橙黄绿青蓝紫循环)
                pixels[i] = wheel((uint8_t)(tick * 2)); 
            } 
            else if (current_effect == 1) {
                // 效果 1：横向幻彩波浪 (纯数学空间相位叠加，绝对无缝)
                pixels[i] = wheel((uint8_t)((tick * 3) + (cx * 4)));
            }
            else if (current_effect == 2) {
                // 效果 2：纵向幻彩波浪 (从上往下如瀑布般流动)
                pixels[i] = wheel((uint8_t)((tick * 3) + (cy * 4)));
            }
        }

        // --- 2. 渲染第 16 颗状态灯 ---
        // 优先级 1：低电量红色闪烁报警 (低于 10%)
        if (battery_level < 10) {
            if (tick % 30 < 15) pixels[STATUS_LED_IDX] = (struct led_rgb){0xFF, 0x00, 0x00};
            else pixels[STATUS_LED_IDX] = (struct led_rgb){0, 0, 0};
        } 
        else if (status_display_frames > 0) {
            uint8_t active_layer = zmk_keymap_highest_layer_active();

            switch (active_layer) {
                case 0: pixels[STATUS_LED_IDX] = (struct led_rgb){0xFF, 0xC0, 0xCB}; break; // 第 0 层：粉色 
                case 1: pixels[STATUS_LED_IDX] = (struct led_rgb){0xFF, 0x80, 0x00}; break; // 第 1 层：橙色 
                case 2: pixels[STATUS_LED_IDX] = (struct led_rgb){0x00, 0xFF, 0x00}; break; // 第 2 层：绿色
                case 3: pixels[STATUS_LED_IDX] = (struct led_rgb){0x00, 0xBF, 0xFF}; break; // 第 3 层：蓝色
                default: pixels[STATUS_LED_IDX] = (struct led_rgb){0xFF, 0xFF, 0xFF}; break; // 其他层：白色 (RGB全开)
            }
            status_display_frames--; // 倒计时递减
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
// 事件监听器
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
