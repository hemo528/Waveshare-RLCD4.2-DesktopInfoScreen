// data_tasks.cpp —— 数据采集任务实现
//
// 任务划分（全部运行在核 1，不触碰 LVGL）：
//   sensor_task prio3 : SHTC3 每 5s、电池每 60s
//   time_task   prio3 : 每秒缓存本地时间；Wi-Fi 通后启动 SNTP；校准成功回写 RTC（此后每 6h 一次）
//   net_task    prio2 : 等 Wi-Fi；拉天气（30min）；可选拉大模型用量（10min）
#include "data_tasks.h"
#include "app_config.h"
#include "data_store.h"
#include "wx_icons.h"   // WX_ICON_* 图标枚举（数据层只传枚举，不碰 LVGL 绘制）

#include <stdio.h>
#include <string.h>
#include <string>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sntp.h"

#include "i2c_bsp.h"
#include "i2c_equipment.h"
#include "adc_bsp.h"
#include "wifi_sta_bsp.h"
#include "ark_quota.h"   // 火山方舟 Coding Plan 用量直查（V4 签名，quota/ 模块）
#include "key_bsp.h"     // 板载按键扫描（页面切换）

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

static const char *TAG = "data";

// ---------- I2C 设备（生命周期与进程同寿，静态持有） ----------
static I2cMasterBus *s_i2c   = nullptr;
static Shtc3Port    *s_shtc3 = nullptr;

// ============================ SNTP 同步回调 ============================
static volatile bool s_sntp_synced = false;

static void sntp_sync_cb(struct timeval *tv)
{
    s_sntp_synced = true;
    ESP_LOGI(TAG, "SNTP synced");
}

// 开机先用 PCF85063 的时间回灌系统时钟：即使没网，时钟也能走起来
static void seed_system_time_from_rtc(void)
{
    rtcTimeStruct_t t;
    Rtc_GetTime(&t);
    if (t.year < 2020 || t.year > 2099) {
        ESP_LOGW(TAG, "RTC time invalid (%d), skip seed", t.year);
        return;
    }
    struct tm tmv = {};
    tmv.tm_year = t.year - 1900;
    tmv.tm_mon  = t.month - 1;
    tmv.tm_mday = t.day;
    tmv.tm_hour = t.hour;
    tmv.tm_min  = t.minute;
    tmv.tm_sec  = t.second;
    tmv.tm_isdst = -1;
    time_t epoch = mktime(&tmv);
    if (epoch > 0) {
        struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "system clock seeded from RTC: %04d-%02d-%02d %02d:%02d:%02d",
                 t.year, t.month, t.day, t.hour, t.minute, t.second);
    }
}

// NTP 校准成功后把时间写进 PCF85063，让下次断电重启也有合理时间
static void write_rtc_from_system(void)
{
    time_t now;
    time(&now);
    struct tm tminfo;
    localtime_r(&now, &tminfo);
    Rtc_SetTime(tminfo.tm_year + 1900, tminfo.tm_mon + 1, tminfo.tm_mday,
                tminfo.tm_hour, tminfo.tm_min, tminfo.tm_sec);
    ESP_LOGI(TAG, "RTC writeback done");
}

// ============================ 任务 1：传感器 ============================
static void sensor_task(void *arg)
{
    int battery_cnt = 0;
    for (;;) {
        float t = 0, h = 0;
        int err = s_shtc3->Shtc3_ReadTempHumi(&t, &h);   // 0 = 成功
        data_set_indoor(t, h, err == 0);

        if (++battery_cnt >= APP_BATTERY_PERIOD_S / APP_SENSOR_PERIOD_S) {
            battery_cnt = 0;
            data_set_battery((int)Adc_GetBatteryLevel());
        }
        vTaskDelay(pdMS_TO_TICKS(APP_SENSOR_PERIOD_S * 1000));
    }
}

// ============================ 任务 2：时间 ============================
static void time_task(void *arg)
{
    bool   sntp_started   = false;
    bool   rtc_calibrated = false;
    time_t last_rtc_write = 0;

    for (;;) {
        // 1) 每秒刷新共享时间缓存
        time_t now;
        time(&now);
        struct tm tminfo;
        localtime_r(&now, &tminfo);
        data_lock();
        g_data.now_tm    = tminfo;
        g_data.ntp_synced = s_sntp_synced;
        data_unlock();

        // 2) Wi-Fi 通了就启动 SNTP（只启动一次）
        if (!sntp_started && wifi_sta_is_up()) {
            esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, APP_NTP_SERVER_1);
            esp_sntp_setservername(1, APP_NTP_SERVER_2);
            sntp_set_time_sync_notification_cb(sntp_sync_cb);
            esp_sntp_init();
            sntp_started = true;
            ESP_LOGI(TAG, "SNTP started");
        }

        // 3) 首次校准成功 → 立即回写 RTC；之后每 APP_RTC_WRITEBACK_HOURS 小时补写一次
        if (s_sntp_synced) {
            if (!rtc_calibrated) {
                write_rtc_from_system();
                rtc_calibrated = true;
                time(&last_rtc_write);
            } else if (now - last_rtc_write > APP_RTC_WRITEBACK_HOURS * 3600) {
                write_rtc_from_system();
                time(&last_rtc_write);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ============================ 任务 3：网络数据 ============================
// 简单阻塞 HTTP GET（https 走 mbedTLS 证书 bundle）
static bool http_get_text(const char *url, std::string &out)
{
    out.clear();
    esp_http_client_config_t cfg = {};
    cfg.url               = url;
    cfg.timeout_ms        = 10000;
    cfg.buffer_size       = 4096;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;   // 公共 CA 集合

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;

    bool ok = false;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        esp_http_client_fetch_headers(client);
        char buf[1024];
        int read_len;
        while ((read_len = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
            out.append(buf, read_len);
            if (out.size() > 32 * 1024) break;   // 防御性上限
        }
        ok = (esp_http_client_get_status_code(client) == 200) && !out.empty();
    } else {
        ESP_LOGE(TAG, "http open failed: %s", esp_err_to_name(err));
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

// WMO weather code → 中文文本（每个词的字必须包含在 font_chinese_16 子集内！
// 若新增词语出现"口口"缺字，需把新字加进 lv_font_conv 的 --symbols 重新生成字库）
static const char *wmo_text(int code)
{
    if (code == 0)              return "晴";
    if (code <= 2)              return "多云";
    if (code == 3)              return "阴";
    if (code == 45 || code == 48) return "雾";
    if (code >= 51 && code <= 57) return "小雨";      // 毛毛雨归入小雨
    if (code == 61)             return "小雨";
    if (code == 63)             return "中雨";
    if (code == 65)             return "大雨";
    if (code == 66 || code == 67) return "冻雨";
    if (code == 71 || code == 77) return "小雪";
    if (code == 73)             return "中雪";
    if (code == 75)             return "大雪";
    if (code >= 80 && code <= 82) return "阵雨";
    if (code == 85 || code == 86) return "阵雪";
    if (code == 95)             return "雷阵雨";
    if (code >= 96)             return "雷阵雨伴冰雹";
    return "多云";
}

// WMO code → 图标枚举（与 wmo_text 一一对应的视觉效果）
static int wmo_icon(int code)
{
    if (code == 0)                return WX_ICON_SUNNY;
    if (code <= 2)                return WX_ICON_PARTLY;
    if (code == 3)                return WX_ICON_OVERCAST;
    if (code == 45 || code == 48) return WX_ICON_FOG;
    if (code == 66 || code == 67) return WX_ICON_SLEET;
    if (code >= 51 && code <= 62) return WX_ICON_RAIN_L;
    if (code <= 67)               return WX_ICON_RAIN_H;    // 63-65
    if (code <= 77)               return WX_ICON_SNOW;      // 71-77
    if (code <= 82)               return WX_ICON_RAIN_H;    // 80-82
    if (code <= 86)               return WX_ICON_SNOW;      // 85-86
    return WX_ICON_THUNDER;                                   // 95-99
}

// 文本关键词 → 图标（OpenWeather 英文 main 字段 / 和风中文 text 字段）。
// 注意顺序：特异性强的词放前面（"雷阵雨"先于"雨"命中）。
// 仅 OpenWeather / 和风数据源使用；Open-Meteo 走上面的 WMO 码直查表
#if APP_WEATHER_PROVIDER != WEATHER_OPEN_METEO
static int wx_icon_from_text(const char *s)
{
    static const struct { const char *kw; int icon; } tab[] = {
        {"thunder", WX_ICON_THUNDER}, {"storm", WX_ICON_THUNDER}, {"雷", WX_ICON_THUNDER},
        {"drizzle", WX_ICON_RAIN_L},  {"小雨", WX_ICON_RAIN_L},   {"毛毛", WX_ICON_RAIN_L},
        {"sleet", WX_ICON_SLEET},     {"冻", WX_ICON_SLEET},
        {"snow", WX_ICON_SNOW},       {"雪", WX_ICON_SNOW},
        {"mist", WX_ICON_FOG}, {"haze", WX_ICON_FOG}, {"fog", WX_ICON_FOG},
        {"雾", WX_ICON_FOG}, {"霾", WX_ICON_FOG}, {"烟", WX_ICON_FOG}, {"尘", WX_ICON_FOG},
        {"overcast", WX_ICON_OVERCAST}, {"阴", WX_ICON_OVERCAST},
        {"cloud", WX_ICON_PARTLY}, {"云", WX_ICON_PARTLY},
        {"rain", WX_ICON_RAIN_H}, {"雨", WX_ICON_RAIN_H},
        {"clear", WX_ICON_SUNNY}, {"晴", WX_ICON_SUNNY},
    };
    for (auto &e : tab) {
        if (strstr(s, e.kw)) return e.icon;
    }
    return WX_ICON_OVERCAST;
}
#endif  // APP_WEATHER_PROVIDER != WEATHER_OPEN_METEO

#if APP_WEATHER_PROVIDER == WEATHER_OPEN_METEO
static bool fetch_weather(void)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast"
             "?latitude=%s&longitude=%s"
             "&current=temperature_2m,relative_humidity_2m,weather_code&timezone=auto",
             APP_WEATHER_LAT, APP_WEATHER_LON);
    std::string body;
    if (!http_get_text(url, body)) return false;

    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) return false;
    cJSON *cur = cJSON_GetObjectItem(root, "current");
    bool ok = false;
    if (cur) {
        cJSON *t = cJSON_GetObjectItem(cur, "temperature_2m");
        cJSON *h = cJSON_GetObjectItem(cur, "relative_humidity_2m");
        cJSON *c = cJSON_GetObjectItem(cur, "weather_code");
        if (cJSON_IsNumber(t) && cJSON_IsNumber(h) && cJSON_IsNumber(c)) {
            data_set_weather(wmo_text((int)c->valuedouble), wmo_icon((int)c->valuedouble),
                             (float)t->valuedouble, (int)h->valuedouble, WX_OPEN_METEO);
            ok = true;
        }
    }
    cJSON_Delete(root);
    return ok;
}
#elif APP_WEATHER_PROVIDER == WEATHER_OPENWEATHER
static bool fetch_weather(void)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.openweathermap.org/data/2.5/weather?q=%s&appid=%s&units=metric",
             APP_WEATHER_OWM_CITY, APP_WEATHER_OWM_KEY);
    std::string body;
    if (!http_get_text(url, body)) return false;

    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) return false;
    bool ok = false;
    cJSON *main = cJSON_GetObjectItem(root, "main");
    cJSON *warr = cJSON_GetObjectItem(root, "weather");
    if (cJSON_IsObject(main) && cJSON_IsArray(warr) && cJSON_GetArraySize(warr) > 0) {
        cJSON *temp = cJSON_GetObjectItem(main, "temp");
        cJSON *humi = cJSON_GetObjectItem(main, "humidity");
        cJSON *cond = cJSON_GetObjectItem(cJSON_GetArrayItem(warr, 0), "main");
        if (cJSON_IsNumber(temp) && cJSON_IsNumber(humi) && cJSON_IsString(cond)) {
            data_set_weather(cond->valuestring, wx_icon_from_text(cond->valuestring),
                             (float)temp->valuedouble, (int)humi->valuedouble, WX_OPENWEATHER);
            ok = true;
        }
    }
    cJSON_Delete(root);
    return ok;
}
#elif APP_WEATHER_PROVIDER == WEATHER_QWEATHER
static bool fetch_weather(void)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://devapi.qweather.com/v7/weather/now?location=%s&key=%s",
             APP_WEATHER_QW_LOC, APP_WEATHER_QW_KEY);
    std::string body;
    if (!http_get_text(url, body)) return false;

    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) return false;
    bool ok = false;
    cJSON *now = cJSON_GetObjectItem(root, "now");
    if (cJSON_IsObject(now)) {
        cJSON *temp = cJSON_GetObjectItem(now, "temp");
        cJSON *humi = cJSON_GetObjectItem(now, "humidity");
        cJSON *text = cJSON_GetObjectItem(now, "text");
        if (cJSON_IsString(text) && cJSON_IsString(temp) && cJSON_IsString(humi)) {
            data_set_weather(text->valuestring, wx_icon_from_text(text->valuestring),
                             atof(temp->valuestring), atoi(humi->valuestring), WX_QWEATHER);
            ok = true;
        }
    }
    cJSON_Delete(root);
    return ok;
}
#endif

// 大模型用量的获取已独立为 quota/ark_quota.cpp（V4 签名直连火山 OpenAPI），
// 这里只负责按周期调用并写进数据仓库。

static void net_task(void *arg)
{
    int wait_cnt = 0;

    // 等 Wi-Fi（最多约 5 分钟，之后放弃网络数据，屏幕照常显示本地方量）
    while (!wifi_sta_is_up() && wait_cnt < 300) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        wait_cnt++;
    }
    if (!wifi_sta_is_up()) {
        ESP_LOGW(TAG, "no WiFi, net data disabled");
        vTaskDelete(NULL);
    }

    // 天气与大模型用量各自独立排期（互不阻塞：一个失败不影响另一个的节奏）
    time_t next_wx    = 0;   // 0 = 进循环立即执行
    time_t next_quota = 0;

    for (;;) {
        time_t now;
        time(&now);

        // ---- 天气（成功按 30min 排期，失败按 5min 重试） ----
        if (now >= next_wx) {
            if (fetch_weather()) {
                next_wx = now + APP_WEATHER_POLL_MIN * 60;
            } else {
                ESP_LOGW(TAG, "weather fetch failed, retry in %d min", APP_WEATHER_RETRY_MIN);
                data_lock();
                g_data.wx_ok = false;
                data_unlock();
                next_wx = now + APP_WEATHER_RETRY_MIN * 60;
            }
        }

        // ---- 大模型用量（成功按 10min 排期，失败 2min 重试；时钟未就绪会内部跳过） ----
#if APP_ARK_ENABLE
        if (now >= next_quota) {
            int remain[3];
            if (ark_quota_fetch(remain)) {
                data_set_quota(remain[0], remain[1], remain[2], "api");
                next_quota = now + APP_LLM_QUOTA_POLL_MIN * 60;
            } else {
                next_quota = now + APP_LLM_QUOTA_RETRY_S;
            }
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(10 * 1000));   // 调度粒度 10s（两个周期都是分钟级，足够）
    }
}

// ============================ 启动入口 ============================
void data_tasks_start(void)
{
    data_store_init();

    // 时区（POSIX 格式，CST-8 = UTC+8）
    setenv("TZ", APP_TIMEZONE, 1);
    tzset();

    // I2C 总线 + 设备
    s_i2c = new I2cMasterBus(APP_I2C_SCL, APP_I2C_SDA, 0);
    Rtc_Setup(s_i2c, APP_RTC_ADDR);
    seed_system_time_from_rtc();     // 先让系统时钟跑起来（RTC 兜底）
    s_shtc3 = new Shtc3Port(*s_i2c);

    // 初始电量
    Adc_PortInit();
    data_set_battery((int)Adc_GetBatteryLevel());

    // 手动配置的大模型用量作为初始值
    data_set_quota(APP_LLM_QUOTA_5H_PCT, APP_LLM_QUOTA_7D_PCT, APP_LLM_QUOTA_30D_PCT, "cfg");

    data_lock();
    snprintf(g_data.wx_city, sizeof(g_data.wx_city), "%s", APP_WEATHER_CITY);
    data_unlock();

    xTaskCreatePinnedToCore(sensor_task, "sensor", 4 * 1024, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(time_task,   "time",   4 * 1024, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(net_task,    "net",    8 * 1024, NULL, 2, NULL, 1);
    key_task_start();   // 按键扫描（20ms 消抖轮询，事件给 LVGL key_poll 消费）
    ESP_LOGI(TAG, "data tasks started");
}
