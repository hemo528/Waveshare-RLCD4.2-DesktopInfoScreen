// config_store.cpp —— 运行时配置存储实现（NVS JSON blob）
#include "config_store.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "cJSON.h"

#include "../app_config.h"

static const char *TAG      = "cfg";
static const char *NS       = "appcfg";
static const char *KEY_JSON = "cfg_json";

static app_cfg_t   s_cfg;
static SemaphoreHandle_t s_mux = NULL;

// ---------- 编译期宏默认值（NVS 为空时的兜底，见 app_config.h） ----------
static void load_defaults(app_cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    strncpy(c->wifi_ssid, APP_WIFI_SSID, sizeof(c->wifi_ssid) - 1);
    strncpy(c->wifi_pass, APP_WIFI_PASSWORD, sizeof(c->wifi_pass) - 1);
    c->wx_provider = APP_WEATHER_PROVIDER;
    strncpy(c->wx_lat, APP_WEATHER_LAT, sizeof(c->wx_lat) - 1);
    strncpy(c->wx_lon, APP_WEATHER_LON, sizeof(c->wx_lon) - 1);
    strncpy(c->wx_city, APP_WEATHER_CITY, sizeof(c->wx_city) - 1);
    strncpy(c->wx_owm_key, APP_WEATHER_OWM_KEY, sizeof(c->wx_owm_key) - 1);
    strncpy(c->wx_owm_city, APP_WEATHER_OWM_CITY, sizeof(c->wx_owm_city) - 1);
    strncpy(c->wx_qw_key, APP_WEATHER_QW_KEY, sizeof(c->wx_qw_key) - 1);
    strncpy(c->wx_qw_loc, APP_WEATHER_QW_LOC, sizeof(c->wx_qw_loc) - 1);
    c->ark_enable  = APP_ARK_ENABLE;
    strncpy(c->ark_ak, APP_ARK_AK, sizeof(c->ark_ak) - 1);
    strncpy(c->ark_sk, APP_ARK_SK, sizeof(c->ark_sk) - 1);
    c->quota_fb[0] = APP_LLM_QUOTA_5H_PCT;
    c->quota_fb[1] = APP_LLM_QUOTA_7D_PCT;
    c->quota_fb[2] = APP_LLM_QUOTA_30D_PCT;
}

// ---------- JSON <-> struct（缺键保留原值，宽容解析） ----------
static void cfg_from_json(app_cfg_t *c, const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "cfg json parse failed, keep defaults");
        return;
    }
    const cJSON *j;
    if ((j = cJSON_GetObjectItem(root, "wifi_ssid")) && cJSON_IsString(j))
        strncpy(c->wifi_ssid, cJSON_GetStringValue(j), sizeof(c->wifi_ssid) - 1);
    if ((j = cJSON_GetObjectItem(root, "wifi_pass")) && cJSON_IsString(j))
        strncpy(c->wifi_pass, cJSON_GetStringValue(j), sizeof(c->wifi_pass) - 1);
    if ((j = cJSON_GetObjectItem(root, "wx_provider")) && cJSON_IsNumber(j))
        c->wx_provider = (int)j->valuedouble;
    if ((j = cJSON_GetObjectItem(root, "wx_lat")) && cJSON_IsString(j))
        strncpy(c->wx_lat, cJSON_GetStringValue(j), sizeof(c->wx_lat) - 1);
    if ((j = cJSON_GetObjectItem(root, "wx_lon")) && cJSON_IsString(j))
        strncpy(c->wx_lon, cJSON_GetStringValue(j), sizeof(c->wx_lon) - 1);
    if ((j = cJSON_GetObjectItem(root, "wx_city")) && cJSON_IsString(j))
        strncpy(c->wx_city, cJSON_GetStringValue(j), sizeof(c->wx_city) - 1);
    if ((j = cJSON_GetObjectItem(root, "wx_owm_key")) && cJSON_IsString(j))
        strncpy(c->wx_owm_key, cJSON_GetStringValue(j), sizeof(c->wx_owm_key) - 1);
    if ((j = cJSON_GetObjectItem(root, "wx_owm_city")) && cJSON_IsString(j))
        strncpy(c->wx_owm_city, cJSON_GetStringValue(j), sizeof(c->wx_owm_city) - 1);
    if ((j = cJSON_GetObjectItem(root, "wx_qw_key")) && cJSON_IsString(j))
        strncpy(c->wx_qw_key, cJSON_GetStringValue(j), sizeof(c->wx_qw_key) - 1);
    if ((j = cJSON_GetObjectItem(root, "wx_qw_loc")) && cJSON_IsString(j))
        strncpy(c->wx_qw_loc, cJSON_GetStringValue(j), sizeof(c->wx_qw_loc) - 1);
    if ((j = cJSON_GetObjectItem(root, "ark_enable")) && cJSON_IsNumber(j))
        c->ark_enable = (int)j->valuedouble ? 1 : 0;
    if ((j = cJSON_GetObjectItem(root, "ark_ak")) && cJSON_IsString(j))
        strncpy(c->ark_ak, cJSON_GetStringValue(j), sizeof(c->ark_ak) - 1);
    if ((j = cJSON_GetObjectItem(root, "ark_sk")) && cJSON_IsString(j))
        strncpy(c->ark_sk, cJSON_GetStringValue(j), sizeof(c->ark_sk) - 1);
    if ((j = cJSON_GetObjectItem(root, "quota_fb")) && cJSON_IsArray(j) &&
        cJSON_GetArraySize(j) == 3) {
        for (int i = 0; i < 3; i++)
            c->quota_fb[i] = (int)cJSON_GetArrayItem(j, i)->valuedouble;
    }
    cJSON_Delete(root);
}

static char *cfg_to_json(const app_cfg_t *c)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "wifi_ssid",  c->wifi_ssid);
    cJSON_AddStringToObject(root, "wifi_pass",  c->wifi_pass);
    cJSON_AddNumberToObject(root, "wx_provider", c->wx_provider);
    cJSON_AddStringToObject(root, "wx_lat",     c->wx_lat);
    cJSON_AddStringToObject(root, "wx_lon",     c->wx_lon);
    cJSON_AddStringToObject(root, "wx_city",    c->wx_city);
    cJSON_AddStringToObject(root, "wx_owm_key", c->wx_owm_key);
    cJSON_AddStringToObject(root, "wx_owm_city", c->wx_owm_city);
    cJSON_AddStringToObject(root, "wx_qw_key",  c->wx_qw_key);
    cJSON_AddStringToObject(root, "wx_qw_loc",  c->wx_qw_loc);
    cJSON_AddNumberToObject(root, "ark_enable", c->ark_enable);
    cJSON_AddStringToObject(root, "ark_ak",     c->ark_ak);
    cJSON_AddStringToObject(root, "ark_sk",     c->ark_sk);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < 3; i++) cJSON_AddItemToArray(arr, cJSON_CreateNumber(c->quota_fb[i]));
    cJSON_AddItemToObject(root, "quota_fb", arr);
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;   // 调用方负责 free()
}

// ---------- NVS 读写 ----------
static bool nvs_write(const app_cfg_t *c)
{
    char *json = cfg_to_json(c);
    if (!json) return false;
    nvs_handle_t h;
    bool ok = false;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        // JSON 里是 UTF-8 且不含 0x00，按字符串存（比 blob 省一个长度字节，也便于串口调试）
        if (nvs_set_str(h, KEY_JSON, json) == ESP_OK && nvs_commit(h) == ESP_OK)
            ok = true;
        nvs_close(h);
    }
    cJSON_free(json);
    if (!ok) ESP_LOGE(TAG, "NVS write failed");
    return ok;
}

void config_store_init(void)
{
    s_mux = xSemaphoreCreateMutex();
    load_defaults(&s_cfg);

    // NVS 初始化（wifi_sta_start 里也会做，nvs_flash_init 幂等，重复调用无害）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = 0;
        if (nvs_get_str(h, KEY_JSON, NULL, &len) == ESP_OK && len > 2 && len < 4096) {
            char *json = (char *)malloc(len);
            if (json && nvs_get_str(h, KEY_JSON, json, &len) == ESP_OK) {
                cfg_from_json(&s_cfg, json);          // 缺键保留默认值
                ESP_LOGI(TAG, "config loaded from NVS (%u bytes), ssid='%s'", (unsigned)len, s_cfg.wifi_ssid);
            } else {
                ESP_LOGW(TAG, "NVS cfg read failed, using defaults");
            }
            free(json);
        } else {
            ESP_LOGI(TAG, "NVS has no config, using defaults%s",
                     s_cfg.wifi_ssid[0] ? "" : "（WiFi 未配置，将进入配网热点模式）");
        }
        nvs_close(h);
    }
}

void cfg_get_copy(app_cfg_t *out)
{
    xSemaphoreTake(s_mux, portMAX_DELAY);
    *out = s_cfg;
    xSemaphoreGive(s_mux);
}

bool cfg_save(const app_cfg_t *c)
{
    xSemaphoreTake(s_mux, portMAX_DELAY);
    s_cfg = *c;
    bool ok = nvs_write(&s_cfg);
    xSemaphoreGive(s_mux);
    ESP_LOGI(TAG, "config saved (%s)", ok ? "nvs ok" : "nvs FAILED");
    return ok;
}

bool cfg_erase(void)
{
    xSemaphoreTake(s_mux, portMAX_DELAY);
    nvs_handle_t h;
    bool ok = false;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, KEY_JSON);           // 键不存在也视为成功
        ok = (nvs_commit(h) == ESP_OK);
        nvs_close(h);
    }
    load_defaults(&s_cfg);                    // 回落宏默认
    xSemaphoreGive(s_mux);
    ESP_LOGW(TAG, "config erased, back to compile-time defaults");
    return ok;
}

// ---------- 网页后台 JSON 接口 ----------
char *cfg_json_dump(void)
{
    xSemaphoreTake(s_mux, portMAX_DELAY);
    char *s = cfg_to_json(&s_cfg);
    xSemaphoreGive(s_mux);
    return s;   // cJSON 打印结果，调用方 cJSON_free()
}

// 限制类字段夹取：provider 1..3、兜底 0..100
static void clamp_cfg(app_cfg_t *c)
{
    if (c->wx_provider < 1 || c->wx_provider > 3) c->wx_provider = 1;
    for (int i = 0; i < 3; i++) {
        if (c->quota_fb[i] < 0)   c->quota_fb[i] = 0;
        if (c->quota_fb[i] > 100) c->quota_fb[i] = 100;
    }
}

bool cfg_apply_json(const char *json)
{
    xSemaphoreTake(s_mux, portMAX_DELAY);
    cfg_from_json(&s_cfg, json);   // 缺键保留现值
    clamp_cfg(&s_cfg);
    bool ok = nvs_write(&s_cfg);
    xSemaphoreGive(s_mux);
    ESP_LOGI(TAG, "config applied from web (%s)", ok ? "nvs ok" : "nvs FAILED");
    return ok;
}
