// web_server.cpp —— 网页管理后台实现（esp_http_server）
#include "web_server.h"

#include <string.h>
#include <stdlib.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

#include "../config/config_store.h"
#include "wifi_sta_bsp.h"
#include "../app_config.h"
#include "web_page.h"

static const char *TAG = "web";

// ---- 工具：JSON 响应 ----
static esp_err_t resp_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

// GET /api/status
static esp_err_t h_status(httpd_req_t *req)
{
    char json[128];
    const char *mode = (wifi_net_mode() == NET_MODE_AP) ? "ap" : "sta";
    const char *ip   = (wifi_net_mode() == NET_MODE_AP) ? "192.168.4.1" : wifi_sta_ip_str();
    app_cfg_t c;
    cfg_get_copy(&c);
    snprintf(json, sizeof(json),
             "{\"mode\":\"%s\",\"ip\":\"%s\",\"ssid\":\"%s\"}",
             mode, ip, (wifi_net_mode() == NET_MODE_AP) ? APP_SETUP_AP_SSID : c.wifi_ssid);
    return resp_json(req, json);
}

// GET /api/config
static esp_err_t h_get_cfg(httpd_req_t *req)
{
    char *json = cfg_json_dump();
    if (!json) return resp_json(req, "{\"error\":\"oom\"}");
    esp_err_t r = resp_json(req, json);
    cJSON_free(json);
    return r;
}

// POST /api/config：保存后 1.5s 重启（网页端显示"已保存"覆盖层）
static esp_err_t h_post_cfg(httpd_req_t *req)
{
    char buf[2048];
    int len = req->content_len;
    if (len <= 0 || len >= (int)sizeof(buf)) return resp_json(req, "{\"ok\":false,\"msg\":\"body too large\"}");
    int got = httpd_req_recv(req, buf, len);
    if (got != len) return resp_json(req, "{\"ok\":false,\"msg\":\"recv failed\"}");
    buf[got] = '\0';

    bool ok = cfg_apply_json(buf);
    if (!ok) return resp_json(req, "{\"ok\":false,\"msg\":\"nvs write failed\"}");

    resp_json(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(1500));   // 让响应完整发出
    esp_restart();                     // 统一重启生效（所有模块从 NVS 重读）
    return ESP_OK;                     // 不可达
}

// POST /api/reset：清除配置 → 回落编译期默认 → 重启
static esp_err_t h_reset(httpd_req_t *req)
{
    cfg_erase();
    resp_json(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    return ESP_OK;
}

// GET /：管理页
static esp_err_t h_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, WEB_PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

static const httpd_uri_t routes[] = {
    { "/",        HTTP_GET,  h_index,   NULL },
    { "/api/status", HTTP_GET,  h_status,  NULL },
    { "/api/config", HTTP_GET,  h_get_cfg, NULL },
    { "/api/config", HTTP_POST, h_post_cfg, NULL },
    { "/api/reset",  HTTP_POST, h_reset,   NULL },
};

void web_server_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.stack_size = 8192;           // cJSON 解析 + 2KB body 缓冲在栈上，默认 4KB 偏紧
    if (httpd_start(&server, &config) == ESP_OK) {
        for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++)
            httpd_register_uri_handler(server, &routes[i]);
        ESP_LOGI(TAG, "web admin ready (http://%s/)",
                 (wifi_net_mode() == NET_MODE_AP) ? "192.168.4.1" : wifi_sta_ip_str());
    } else {
        ESP_LOGE(TAG, "httpd start failed");
    }
}
