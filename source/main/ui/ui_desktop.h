// ui_desktop.h —— 桌面信息屏 UI
#pragma once

#include "lvgl.h"

// 创建整屏 UI 并注册 1s 的 ui_tick 定时器。
// 必须在 Lvgl_PortInit() 之后、持锁状态下调用：
//   if (Lvgl_lock(-1)) { ui_desktop_create(); Lvgl_unlock(); }
void ui_desktop_create(void);
