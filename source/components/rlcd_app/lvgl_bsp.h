// lvgl_bsp.h —— LVGL v8 移植层（移植自微雪官方例程，未改动）
#pragma once

#include "lvgl.h"

#define LVGL_TICK_PERIOD_MS    5
#define LVGL_TASK_MAX_DELAY_MS 500
// 50ms 的最小间隔会把动画帧率限死在 ~14fps（翻页发顿的元凶）。
// 空闲时 lv_timer_handler 返回"下一个定时器到期还差多久"，不会空转；
// 只有动画进行中才会以 5ms 节拍连续出帧 —— 动画帧率 ≈ 40fps
#define LVGL_TASK_MIN_DELAY_MS 5

typedef void (*DispFlushCb)(struct _lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);

void Lvgl_PortInit(int width, int height,DispFlushCb flush_cb);
bool Lvgl_lock(int timeout_ms);
void Lvgl_unlock(void);
