// data_store.h —— 各任务共享的数据仓库
//
// 线程模型（重要）：
//   * 所有 FreeRTOS 任务只【写】本仓库（需持有 data_lock）
//   * UI 只在 LVGL 定时器（ui_tick）里【读】本仓库（同样需持有 data_lock）
//     而 LVGL 控件本身只在 lv_timer 回调里更新 —— LVGL 任务内部天然串行，无需再对控件加锁
#pragma once

#include <time.h>
#include <stdint.h>
#include <stdbool.h>

// 天气数据源状态
enum WeatherSource {
    WX_NONE = 0,
    WX_OPEN_METEO,
    WX_OPENWEATHER,
    WX_QWEATHER,
};

struct data_store_t {
    // ---- 室内传感器 (SHTC3) ----
    float    indoor_temp_c = 0;
    float    indoor_rh     = 0;
    bool     indoor_ok     = false;

    // ---- 电池 ----
    int      batt_pct      = -1;      // -1 = 未知

    // ---- 时间 ----
    struct tm now_tm        = {};      // 本地时间（秒级）
    bool     ntp_synced     = false;   // true=NTP 已校准  false=RTC 兜底

    // ---- 天气 ----
    char     wx_city[24]    = "";      // 城市名（来自配置）
    char     wx_text[24]    = "--";    // 天气现象文本
    int      wx_icon        = 0;       // 天气图标枚举（wx_icons.h 的 WX_ICON_*）
    float    wx_temp_c      = 0;
    int      wx_rh          = -1;
    bool     wx_ok          = false;
    int      wx_source      = WX_NONE;
    time_t   wx_ts          = 0;       // 最近一次天气更新时刻（epoch）

    // ---- 大模型用量（0~100，剩余百分比） ----
    // index: 0=5 小时窗口  1=7 天  2=30 天
    int      quota_pct[3]   = {0, 0, 0};
    char     quota_src[8]   = "cfg";   // "cfg"=手动配置  "api"=接口获取
};

extern data_store_t g_data;

void data_store_init(void);
void data_lock(void);
void data_unlock(void);

// 便捷写入接口（内部自动加锁）
void data_set_indoor(float t, float rh, bool ok);
void data_set_battery(int pct);
void data_set_weather(const char *text, int icon, float temp, int rh, int source);
void data_set_quota(int slot5h, int slot7d, int slot30d, const char *src);
