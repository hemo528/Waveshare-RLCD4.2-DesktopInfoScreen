// wifi_sta_bsp.h —— WiFi 网络管理（STA + 配网 SoftAP）
//
// STA：正常模式；SoftAP：未配置/连接失败时的配网热点（APSTA 可与 STA 共存）。
// 断线自动重连；连接状态/IP/运行模式提供查询接口。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NET_MODE_NONE = 0,   // WiFi 尚未启动
    NET_MODE_STA,        // 工作模式（连路由器）
    NET_MODE_AP,         // 配网模式（热点开启，192.168.4.1）
} net_mode_t;

// 启动 STA（非阻塞；连接动作在后台异步进行）。ssid 为空时不做任何事（留在配网模式）
void wifi_sta_start(const char *ssid, const char *password);

// 启动配网热点（WPA2；密码 <8 字节自动变为开放网络）。可在 STA 之后调用（切 APSTA 共存）
void wifi_ap_start(const char *ssid, const char *password);

// 阻塞等待 STA 拿到 IP（仅 STA 模式有效）；超时返回 false
bool wifi_sta_wait_up(uint32_t timeout_ms);

bool        wifi_sta_is_up(void);     // 是否已获取 IP
const char *wifi_sta_ip_str(void);    // IP 字符串；未连接返回 "--"
net_mode_t  wifi_net_mode(void);      // 当前运行形态
bool        wifi_ap_active(void);     // 配网热点是否开启

#ifdef __cplusplus
}
#endif
