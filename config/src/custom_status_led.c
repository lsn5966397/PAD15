#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/keycode_state_changed.h> // 新增：用于监听按键

#define STRIP_NODE DT_CHOSEN(zmk_underglow)
#define NUM_PIXELS 16
#define STATUS_LED_IDX 15

static const struct device *led_strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[NUM_PIXELS];

static uint8_t current_layer = 0;
static uint8_t battery_level = 100;
static int status_display_frames = 0;

// 新增：灯效模式状态 (0: 彩虹, 1: 纯色呼吸, 2: 常亮熄灭)
static uint8_t current_effect = 0; 
#define MAX_EFFECTS 3

// HSV 转 RGB 算法... (和之前一样)
static struct led_rgb wheel(uint8_t pos) {
    pos = 255 - pos;
    if (pos < 85) return (struct led_rgb){255 - pos * 3, 0, pos * 3};
    else if (pos < 170) { pos -= 85; return (struct led_rgb){0, pos * 3, 255 - pos * 3}; }
    else { pos -= 170; return (struct led_rgb){pos * 3, 255 - pos * 3, 0}; }
}

// 核心动画线程
void custom_led_thread_main(void) {
    if (!device_is_ready(led_strip)) return;

    uint8_t tick = 0; 

    while (1) {
        // ==========================================
        // 1. 渲染前 15 颗灯：根据 current_effect 切换逻辑
        // ==========================================
        for (int i = 0; i < STATUS_LED_IDX; i++) {
            if (current_effect == 0) {
                // 特效 0：流动彩虹
                pixels[i] = wheel((uint8_t)(tick + i * 15));
            } 
            else if (current_effect == 1) {
                // 特效 1：蓝色呼吸 (利用三角函数或简单的折返逻辑)
                // tick 从 0-255 循环，我们做一个简单的亮度渐变
                uint8_t brightness = (tick < 128) ? (tick * 2) : ((255 - tick) * 2);
                pixels[i] = (struct led_rgb){0, 0, brightness}; 
            }
            else {
                // 特效 2：全部熄灭 (省电模式)
                pixels[i] = (struct led_rgb){0, 0, 0};
            }
        }

        // ==========================================
        // 2. 渲染第 16 颗灯：状态覆盖逻辑 (不可侵犯的绝对优先级)
        // ==========================================
        if (battery_level < 20) {
            if (tick % 10 < 5) pixels[STATUS_LED_IDX] = (struct led_rgb){0xFF, 0x00, 0x00};
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
            // 正常状态下，第 16 颗灯也可以跟着变，或者保持熄灭
            pixels[STATUS_LED_IDX] = (struct led_rgb){0, 0, 0}; 
        }

        led_strip_update_rgb(led_strip, pixels, NUM_PIXELS);
        tick++;
        k_sleep(K_MSEC(30)); 
    }
}
K_THREAD_DEFINE(custom_led_tid, 1024, custom_led_thread_main, NULL, NULL, NULL, 7, 0, 0);

// ==========================================
// 新增事件监听器：监听按键切换灯效
// ==========================================
static int keycode_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    
    // 如果有按键被按下 (ev->state 为 true)
    if (ev && ev->state) {
        // 假设我们用 F20 这个键来作为切换键 
        // (HID Usage ID: 0x6D 就是 F20)
        if (ev->keycode == 0x6D) { 
            current_effect++;
            if (current_effect >= MAX_EFFECTS) {
                current_effect = 0;
            }
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(keycode_status, keycode_listener);
ZMK_SUBSCRIPTION(keycode_status, zmk_keycode_state_changed);

/* =======================================
 * 事件监听器：层切换
 * ======================================= */
static int layer_status_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (ev && ev->state) { 
        current_layer = ev->layer;
        
        // 触发工作队列，点亮灯光
        is_blinking = true; 
        k_work_reschedule(&status_led_work, K_NO_WAIT);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
// 注册监听器
ZMK_LISTENER(layer_status, layer_status_listener);
ZMK_SUBSCRIPTION(layer_status, zmk_layer_state_changed);

/* =======================================
 * 事件监听器：电池状态
 * ======================================= */
static int battery_status_listener(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    if (ev) {
        battery_level = ev->state_of_charge;
        
        // 如果电量低于20%，立刻唤醒工作队列开始闪烁
        if (battery_level < 20) {
            k_work_reschedule(&status_led_work, K_NO_WAIT);
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}
// 注册监听器
ZMK_LISTENER(battery_status, battery_status_listener);
ZMK_SUBSCRIPTION(battery_status, zmk_battery_state_changed);

/* =======================================
 * 系统初始化
 * ======================================= */
static int custom_status_led_init(const struct device *dev) {
    // 初始化延时工作队列
    k_work_init_delayable(&status_led_work, update_status_led);
    return 0;
}
// 设定初始化优先级，确保在设备树解析后执行
SYS_INIT(custom_status_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
