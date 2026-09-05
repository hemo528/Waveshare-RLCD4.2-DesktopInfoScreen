// wifi_sta_bsp.h —— 精简版 WiFi STA 模块（参考官方 esp_wifi_bsp 重写）
//
// 与官方版本的区别：
//   1. 凭据由参数传入（main 从 app_config.h 取），不再写死在 BSP 里
//   2. 去掉了 BLE 扫描与 AP 计数逻辑
//   3. 断线自动重连，连接状态/IP 提供查询接口
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 非阻塞启动：内部完成 NVS/事件循环/WiFi 初始化，连接动作在后台异步进行
void wifi_sta_start(const char *ssid, const char *password);

bool        wifi_sta_is_up(void);     // 是否已获取 IP
const char *wifi_sta_ip_str(void);    // 获取 IP 字符串；未连接返回 "--"

#ifdef __cplusplus
}
#endif
