// key_bsp.cpp —— 按键 GPIO 扫描与消抖实现
#include "key_bsp.h"

#include "app_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <stdatomic.h>

static atomic_int s_event = KEY_EVT_NONE;   // 原子事件槽（单生产者单消费者）

void key_task_start(void)
{
    // 两颗键同配置：输入 + 内部上拉（板上按键对地，低电平有效）
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << APP_KEY_PREV_IO) | (1ULL << APP_KEY_NEXT_IO);
    io.mode         = GPIO_MODE_INPUT;
    io.pull_up_en   = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_DISABLE;      // 轮询消抖，不用中断（省去 ISR 复杂度）
    gpio_config(&io);

    xTaskCreatePinnedToCore([](void *) {
        // 消抖：连续 N 次采样同电平才确认稳定；只在"稳定按下"的前沿发事件
        int prev_stable[2] = { 1, 1 };        // 1 = 未按（高电平）
        int run_len[2]     = { 0, 0 };
        const gpio_num_t pins[2] = { (gpio_num_t)APP_KEY_PREV_IO, (gpio_num_t)APP_KEY_NEXT_IO };
        const int evt[2]         = { KEY_EVT_PREV, KEY_EVT_NEXT };
        const int DEBOUNCE = 3;               // 3 × 20ms = 60ms 稳定期

        for (;;) {
            for (int i = 0; i < 2; i++) {
                int level = gpio_get_level(pins[i]);   // 0=按下
                if (level == prev_stable[i]) {
                    run_len[i] = 0;
                    continue;
                }
                if (++run_len[i] >= DEBOUNCE) {
                    prev_stable[i] = level;
                    run_len[i] = 0;
                    if (level == 0) {             // 按下沿：发事件（覆盖旧事件，不排队）
                        atomic_store(&s_event, evt[i]);
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }, "key", 2 * 1024, NULL, 3, NULL, 1);
}

enum key_event_t key_consume_event(void)
{
    return (enum key_event_t)atomic_exchange(&s_event, KEY_EVT_NONE);
}
