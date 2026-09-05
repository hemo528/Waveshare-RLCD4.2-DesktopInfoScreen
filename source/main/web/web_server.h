// web_server.h —— 网页管理后台（HTTP 服务）
// 在 STA（屏幕 IP）与 SoftAP（192.168.4.1）两种模式下都可访问。
// 路由：
//   GET  /            管理页（内嵌 HTML）
//   GET  /api/status  {"mode":"sta|ap","ip":"..","ssid":".."}
//   GET  /api/config  当前配置 JSON
//   POST /api/config  保存配置（JSON），成功后 1.5s 自动重启生效
//   POST /api/reset   清除配置回落编译期默认，重启（未配置则进入配网热点）
#pragma once

void web_server_start(void);   // 阻塞极短（httpd_start），在 WiFi 形态确定后调用一次
