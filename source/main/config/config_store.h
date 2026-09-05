// config_store.h —— 运行时配置存储（NVS 持久化，网页后台读写）
//
// 设计：
//   - 全部可调配置打包成一个 JSON blob 存在 NVS（命名空间 "appcfg"，键 "cfg_json"）
//   - config_store_init()：上电加载；NVS 为空时回落到 app_config.h 编译期宏值
//     （已烧好 WiFi 的旧板升级固件后行为不变；模板固件宏值为空 → 自动进配网热点）
//   - 网页后台 POST 保存 → cfg_save() 写 NVS 并更新内存 → 主控决定重启生效
//   - 读配置一律走 cfg_get_copy()（互斥锁内整 struct 拷贝），避免读到改了一半的值
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // WiFi
    char wifi_ssid[33];
    char wifi_pass[64];
    // 天气
    int  wx_provider;          // 1=Open-Meteo 2=OpenWeather 3=和风
    char wx_lat[16];
    char wx_lon[16];
    char wx_city[32];          // 界面显示的城市名（UTF-8；须在子集字库内）
    char wx_owm_key[48];
    char wx_owm_city[48];
    char wx_qw_key[48];
    char wx_qw_loc[16];
    // 大模型用量（火山方舟 Coding Plan）
    int  ark_enable;           // 1=直连查询
    char ark_ak[80];
    char ark_sk[80];
    // 手动兜底剩余 %（仅启动瞬间/接口长期失败时显示）
    int  quota_fb[3];
} app_cfg_t;

// NVS 初始化 + 配置装载（NVS 空则写入宏默认值并落盘）。整个系统只调一次。
void config_store_init(void);

// 拷贝当前配置（互斥锁内 memcpy，调用方随便用）
void cfg_get_copy(app_cfg_t *out);

// 保存：更新内存 + 写 NVS blob。成功返回 true
bool cfg_save(const app_cfg_t *c);

// 清除 NVS 里的配置并回落宏默认值（网页"恢复出厂"用；不自动重启）
bool cfg_erase(void);

// ---- 网页后台用 JSON 接口 ----
// 序列化当前配置为 JSON（cJSON 打印；调用方 cJSON_free()）
char *cfg_json_dump(void);

// 用 JSON 更新配置并保存（缺键保留现值；provider 夹取 1..3，兜底值夹取 0..100）
bool cfg_apply_json(const char *json);

#ifdef __cplusplus
}
#endif
