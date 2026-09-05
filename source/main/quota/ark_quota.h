// ark_quota.h —— 火山方舟 Coding Plan 用量直查（ESP32 直连，无电脑中转）
//
// 数据链路（全部已在 PC 端原型验证通过，见 开发工具/ark_quota_proto.py）：
//   POST https://open.volcengineapi.com/?Action=GetCodingPlanUsage&Version=2024-01-01
//   Body: "{}"
//   鉴权: 火山 V4 签名（静态 AK/SK，HMAC-SHA256 链，无 session token）
//   响应: Result.QuotaUsage[{Level:"session"/"weekly"/"monthly", Percent, ResetTimestamp}]
//         Percent 是【已用】百分比，本模块负责换算成【剩余】= 100 - used
//
// 签名参考实现：volc-sdk-python volcengine/auth/SignerV4.py（官方源码逐行核对），
// ESP32 上用 mbedTLS 的 md API 复刻同样的 HMAC-SHA256 链。
#pragma once

#include <stdbool.h>

// 拉取一次套餐用量快照。
// remain[0]=5小时窗口(session) remain[1]=周(weekly) remain[2]=月(monthly)
// 单位：剩余百分比 0~100（已用由接口返回后换算）
// 返回 true = 三个值全部有效；false = 网络/签名/解析任一环节失败
bool ark_quota_fetch(int remain[3]);
