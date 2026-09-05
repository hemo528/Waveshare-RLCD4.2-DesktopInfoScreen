// app_config.h —— ⭐ 全部可调配置集中在这里
// 标注 [待确认] 的项请按实际情况修改
#pragma once

// ---------------- Wi-Fi ----------------
// 注意：ESP32-S3 只支持 2.4GHz！若这个 "_5G" SSID 是路由器的 5GHz 频段，
// 板子搜不到它——请确认路由器把 2.4G 与 5G 合并为同名，或改用 2.4G 的 SSID。
#define APP_WIFI_SSID       "你的WiFi名称"      // WiFi 名称（2.4GHz，UTF-8 共 20 字节 < 32 上限）
#define APP_WIFI_PASSWORD   "你的WiFi密码"            // WiFi 密码

// ---------------- 时间 ----------------
#define APP_NTP_SERVER_1    "ntp.aliyun.com"      // 国内优先阿里 NTP
#define APP_NTP_SERVER_2    "pool.ntp.org"
#define APP_TIMEZONE        "CST-8"               // POSIX TZ 格式：UTC+8
#define APP_RTC_WRITEBACK_HOURS 6                 // NTP 校准后回写 PCF85063 的周期（小时）

// ---------------- 硬件引脚（来自官方原理图/例程，一般无需改动） ----------------
#define APP_I2C_SCL         14
#define APP_I2C_SDA         13
#define APP_RTC_ADDR        0x51                  // PCF85063
#define APP_DISP_MOSI       12
#define APP_DISP_CLK        11
#define APP_DISP_DC         5
#define APP_DISP_CS         40
#define APP_DISP_RST        41
#define APP_DISP_TE         6    // 面板 Tearing Effect 输出（原理图 NLLCD0TE），帧同步写 GRAM 用

// ---------------- 天气 ----------------
// 数据源三选一：
//   1 = Open-Meteo（默认，免费无需 key，开箱即用）
//   2 = OpenWeather（需要 APP_WEATHER_OWM_KEY）
//   3 = 和风天气   （需要 APP_WEATHER_QW_KEY 与城市 ID）
#define WEATHER_OPEN_METEO  1
#define WEATHER_OPENWEATHER 2
#define WEATHER_QWEATHER    3
#define APP_WEATHER_PROVIDER  WEATHER_OPEN_METEO  // [待确认] 天气数据源

#define APP_WEATHER_CITY    "南京"                // 界面显示的城市名（子集字库已含"南京"；换城市需重新生成字库）
#define APP_WEATHER_LAT     "32.06"               // 南京纬度（Open-Meteo 用）
#define APP_WEATHER_LON     "118.79"              // 南京经度（Open-Meteo 用）
#define APP_WEATHER_OWM_KEY ""                    // OpenWeather API key
#define APP_WEATHER_OWM_CITY  "nanjing,CN"        // OpenWeather 的 q= 参数
#define APP_WEATHER_QW_KEY    ""                  // 和风天气 key
#define APP_WEATHER_QW_LOC    "101190101"         // 和风 LocationID（南京，其余城市见和风文档）
#define APP_WEATHER_POLL_MIN  30                  // 天气刷新周期（分钟）
#define APP_WEATHER_RETRY_MIN 5                   // 失败重试周期（分钟）

// ---------------- 大模型用量（火山引擎 Coding Plan，ESP32 直连官方 API） ----------------
// 链路（2026-09 实测打通）：AK/SK V4 签名 → OpenTOP GetCodingPlanUsage → 剩余百分比
//   接口只返回"已用 %"（无次数明细），固件自动换算为"剩余 %"上屏。
//   三窗口映射：session(5小时滚动窗)/weekly(周一 00:00 重置)/monthly(订阅月末日重置)
#define APP_ARK_ENABLE       1                    // 1=直连查询  0=只用下面手动兜底值
#define APP_ARK_AK           ""
#define APP_ARK_SK           ""
// ⚠️ 安全：此密钥曾出现在聊天记录，稳定后建议到控制台轮换，并用只读权限的 IAM 子用户密钥替换

// 手动兜底值：仅启动瞬间 / API 长期失败时显示（拿到 API 数据即被覆盖）
#define APP_LLM_QUOTA_5H_PCT   100                 // 5 小时窗口剩余 %（兜底）
#define APP_LLM_QUOTA_7D_PCT   100                 // 7 天剩余 %（兜底）
#define APP_LLM_QUOTA_30D_PCT  100                 // 30 天剩余 %（兜底）
#define APP_LLM_QUOTA_POLL_MIN 10                  // 用量接口轮询周期（分钟）
#define APP_LLM_QUOTA_RETRY_S  120                 // 查询失败重试间隔（秒）

// ---------------- 按键（页面切换） ----------------
// 板上三键实测（原理图 + 官方 Demo 双重确认）：
//   Key1 = GPIO0（丝印 BOOT）  Key4 = GPIO18（丝印 KEY/USER）——低电平按下，均可编程
//   Key3 接电源管理芯片（电源/复位类），软件读不到，不能用作切页
// 若左右方向与手感相反，把下面两个 IO 号对调重编译即可
#define APP_KEY_PREV_IO      0                    // 上一页
#define APP_KEY_NEXT_IO      18                   // 下一页

// ---------------- 任务刷新周期 ----------------
#define APP_SENSOR_PERIOD_S    5                   // SHTC3 采样周期（秒）
#define APP_BATTERY_PERIOD_S   60                  // 电池电量周期（秒）
#define APP_UI_TICK_MS         1000                // UI 刷新节拍（毫秒）
