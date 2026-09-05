// key_bsp.h —— 板载用户按键扫描（页面切换）
//
// 硬件（原理图 + 官方 Demo 确认）：GPIO0（BOOT 丝印）与 GPIO18（KEY 丝印），
// 均为低电平按下（板上有上拉）。第三颗键接电源管理芯片，软件不可用。
// 注意：GPIO0 按住上电会进下载模式，运行期短按无影响。
//
// 线程模型：key_task 20ms 轮询 + 消抖，产生"按下沿"事件写入原子变量；
// LVGL 侧（ui 的 100ms 定时器）调用 key_consume_event() 取走事件。
// 事件只会被取走一次，不堆积（连按多下只保留最后一方向——切页场景足够）。
#pragma once

#include <stdbool.h>

enum key_event_t {
    KEY_EVT_NONE = 0,
    KEY_EVT_PREV,       // 上一页（GPIO0）
    KEY_EVT_NEXT,       // 下一页（GPIO18）
};

// 创建按键扫描任务（在 data_tasks_start 前后调用均可）
void key_task_start(void);

// 取走最近一次按键事件（原子交换，取后清零）；无事件返回 KEY_EVT_NONE
enum key_event_t key_consume_event(void);
