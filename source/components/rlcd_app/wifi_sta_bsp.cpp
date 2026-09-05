// wifi_sta_bsp.cpp —— 精简版 WiFi STA 模块
#include <string.h>
#include "wifi_sta_bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"

#define WIFI_UP_BIT   BIT0

static const char *TAG = "wifi_sta";
static EventGroupHandle_t s_ev = NULL;
static char s_ip[16] = "--";

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();                                   // 开始连接
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_ev, WIFI_UP_BIT);
        strcpy(s_ip, "--");
        ESP_LOGW(TAG, "disconnected, retrying...");
        esp_wifi_connect();                                   // 断线自动重连
    }
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
    esp_ip4addr_ntoa(&evt->ip_info.ip, s_ip, sizeof(s_ip));
    xEventGroupSetBits(s_ev, WIFI_UP_BIT);
    ESP_LOGI(TAG, "got IP: %s", s_ip);
}

void wifi_sta_start(const char *ssid, const char *password)
{
    // 空凭据守卫：app_config.h 未填 WiFi 时（例如公开发布的模板固件）直接跳过，
    // 避免 esp_wifi_set_config 对空 SSID 报错触发 ESP_ERROR_CHECK abort
    if (ssid == nullptr || ssid[0] == '\0') {
        ESP_LOGW(TAG, "WiFi SSID empty (check app_config.h), WiFi disabled");
        return;
    }
    s_ev = xEventGroupCreate();

    // NVS：WiFi 驱动需要；首次烧录可能无有效分区，擦除重试
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL));

    wifi_config_t wc = {};
    strncpy((char *)wc.sta.ssid,     ssid,     sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, password, sizeof(wc.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_start());
}

bool wifi_sta_is_up(void)
{
    return (s_ev != NULL) && (xEventGroupGetBits(s_ev) & WIFI_UP_BIT);
}

const char *wifi_sta_ip_str(void)
{
    return s_ip;
}
