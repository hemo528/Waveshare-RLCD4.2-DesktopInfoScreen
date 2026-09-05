// wifi_sta_bsp.cpp —— WiFi 网络管理（STA + 配网 SoftAP）
//
// 两种运行形态：
//   STA      ：正常工作模式，连家里路由器，屏幕状态栏显示拿到的 IP
//   SoftAP   ：配网模式（未配置 / STA 45s 拿不到 IP 时回退），手机连热点后
//              浏览器访问 192.168.4.1 进管理后台；热点与 STA 可共存（APSTA），
//              用户在后台改 WiFi 的同时设备仍会继续尝试连接原路由
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

static const char *TAG = "wifi";
static EventGroupHandle_t s_ev = NULL;
static char s_ip[16] = "--";
static volatile net_mode_t s_mode = NET_MODE_NONE;

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

// 公共初始化：NVS / netif / 事件循环 / wifi 驱动（幂等，只做一次）
static esp_netif_t *s_netif_sta = NULL;
static esp_netif_t *s_netif_ap  = NULL;
static bool s_wifi_inited = false;

static void wifi_core_init(void)
{
    if (s_wifi_inited) return;
    s_ev = xEventGroupCreate();

    // NVS：WiFi 驱动需要；首次烧录可能无有效分区，擦除重试
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif_sta = esp_netif_create_default_wifi_sta();
    s_netif_ap  = esp_netif_create_default_wifi_ap();   // 两个 netif 都建好，随时切换

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL));
    s_wifi_inited = true;
}

void wifi_sta_start(const char *ssid, const char *password)
{
    // 空凭据守卫：调用方应转配网热点模式，这里不碰 wifi
    if (ssid == nullptr || ssid[0] == '\0') {
        ESP_LOGW(TAG, "WiFi SSID empty, stay in setup AP mode");
        return;
    }
    wifi_core_init();

    wifi_config_t wc = {};
    strncpy((char *)wc.sta.ssid,     ssid,     sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, password, sizeof(wc.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_mode = NET_MODE_STA;
}

// 配网热点：密码 >=8 字节走 WPA2，否则开放网络。已在 STA 模式时切 APSTA 共存（不断 STA 重试）
void wifi_ap_start(const char *ssid, const char *password)
{
    wifi_core_init();

    wifi_config_t wc = {};
    strncpy((char *)wc.ap.ssid,     ssid,     sizeof(wc.ap.ssid) - 1);
    wc.ap.ssid_len     = strlen(ssid);
    wc.ap.max_connection = 4;
    wc.ap.authmode     = WIFI_AUTH_OPEN;
    if (password && strlen(password) >= 8) {
        strncpy((char *)wc.ap.password, password, sizeof(wc.ap.password) - 1);
        wc.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    }

    // AP-only 尚未 start 过 → AP；STA 已在跑 → APSTA（保留后台重连路由）
    if (s_mode == NET_MODE_STA) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    } else {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_start());
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    s_mode = NET_MODE_AP;
    ESP_LOGW(TAG, "setup AP started: ssid='%s' (%s), open http://192.168.4.1/",
             ssid, wc.ap.authmode == WIFI_AUTH_OPEN ? "open" : "wpa2");
}

// 等待 STA 拿到 IP（配网决策用）
bool wifi_sta_wait_up(uint32_t timeout_ms)
{
    if (s_ev == NULL || s_mode != NET_MODE_STA) return false;
    EventBits_t bits = xEventGroupWaitBits(s_ev, WIFI_UP_BIT, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_UP_BIT) != 0;
}

bool wifi_sta_is_up(void)
{
    return (s_ev != NULL) && (xEventGroupGetBits(s_ev) & WIFI_UP_BIT);
}

const char *wifi_sta_ip_str(void)
{
    return s_ip;
}

net_mode_t wifi_net_mode(void)
{
    return s_mode;
}

bool wifi_ap_active(void)
{
    return s_mode == NET_MODE_AP;
}
