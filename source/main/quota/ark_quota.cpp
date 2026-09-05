// ark_quota.cpp —— 火山方舟 OpenAPI V4 签名 + Coding Plan 用量查询（ESP32 直连）
//
// 签名流程与 volc-sdk-python 的 auth/SignerV4.py 逐行对应：
//   1) 规范请求 = 方法\n路径\n查询串\n规范头(小写,排序,每个带\n)\n签名头名单\nbody哈希
//      注意：规范头块自带末尾 \n，join 又补一个 \n → 头块和名单之间有一个空行，不能少
//   2) 待签串 = "HMAC-SHA256" \n X-Date \n 日期/cn-beijing/ark/request \n sha256(规范请求)
//      （与 AWS SigV4 的差异：第二行直接是完整 X-Date，第三行是 credential scope）
//   3) 密钥链 = HMAC(SK,日期) → HMAC(,区域) → HMAC(,服务) → HMAC(,"request")
//   4) Authorization = HMAC-SHA256 Credential=AK/scope, SignedHeaders=..., Signature=hex
//
// 头集合固定四个（官方 SignerV4 只挑 Content-Type/Content-Md5/Host/X-* 参与签名）：
//   content-type / host / x-content-sha256 / x-date
// 查询串是固定字面量（Action/Version 值全部是 unreserved 字符），无需动态 URI 编码。
#include "ark_quota.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <string>

#include "app_config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "mbedtls/md.h"     // mbedtls_md_hmac 一次性 HMAC
#include "mbedtls/sha256.h" // mbedtls_sha256   一次性哈希

static const char *TAG = "quota";

// ---------- OpenTOP 端点常量（全部实测核对） ----------
static const char *ARK_HOST     = "open.volcengineapi.com";
static const char *ARK_QUERY    = "Action=GetCodingPlanUsage&Version=2024-01-01";
static const char *ARK_REGION   = "cn-beijing";
static const char *ARK_SERVICE  = "ark";
static const char *ARK_BODY     = "{}";

// ---------- 密码学小工具 ----------
static void sha256_hex(const void *in, size_t len, char out_hex[65])
{
    unsigned char dig[32];
    mbedtls_sha256((const unsigned char *)in, len, dig, 0 /* 0 = SHA-256 */);
    for (int i = 0; i < 32; i++) {
        snprintf(out_hex + i * 2, 3, "%02x", dig[i]);
    }
}

static void hmac_sha256(const unsigned char *key, size_t klen,
                        const void *msg, size_t mlen, unsigned char out[32])
{
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_hmac(md, key, klen, (const unsigned char *)msg, mlen, out);
}

// ---------- V4 签名 ----------
// 输入：X-Date（UTC，格式 YYYYMMDDThhmmssZ），body 哈希
// 输出：Authorization 头的完整值
static void v4_sign(const char *x_date, const char *body_hash, char auth[512])
{
    char datebuf[9];
    snprintf(datebuf, sizeof(datebuf), "%.8s", x_date);

    // 1) 规范头（名字小写、按字典序、每个值后跟 \n）
    //    固定顺序 = 字典序：content-type < host < x-content-sha256 < x-date
    char canon[640];
    snprintf(canon, sizeof(canon),
             "POST\n"                               // 方法
             "/\n"                                   // 规范路径
             "%s\n"                                  // 规范查询串
             "content-type:application/json\n"
             "host:%s\n"
             "x-content-sha256:%s\n"
             "x-date:%s\n"
             "\n"                                    // 头块结束的空行（SignerV4 的 join 结构）
             "content-type;host;x-content-sha256;x-date\n"  // 签名头名单
             "%s",                                   // body 哈希
             ARK_QUERY, ARK_HOST, body_hash, x_date, body_hash);

    char canon_hash[65];
    sha256_hex(canon, strlen(canon), canon_hash);

    // 2) 待签串：算法 \n X-Date \n scope \n sha256(规范请求)
    char to_sign[256];
    snprintf(to_sign, sizeof(to_sign),
             "HMAC-SHA256\n%s\n%.8s/%s/%s/request\n%s",
             x_date, datebuf, ARK_REGION, ARK_SERVICE, canon_hash);

    // 3) 密钥链：SK → 日期 → 区域 → 服务 → "request"
    unsigned char k1[32], k2[32], k3[32], k4[32];
    hmac_sha256((const unsigned char *)APP_ARK_SK, strlen(APP_ARK_SK), datebuf, 8, k1);
    hmac_sha256(k1, 32, ARK_REGION, strlen(ARK_REGION), k2);
    hmac_sha256(k2, 32, ARK_SERVICE, strlen(ARK_SERVICE), k3);
    hmac_sha256(k3, 32, "request", 7, k4);

    // 4) 签名 + Authorization 头
    char sig_hex[65];
    {
        unsigned char dig[32];
        mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                        k4, 32, (const unsigned char *)to_sign, strlen(to_sign), dig);
        for (int i = 0; i < 32; i++) {
            snprintf(sig_hex + i * 2, 3, "%02x", dig[i]);
        }
    }
    snprintf(auth, 512,
             "HMAC-SHA256 Credential=%s/%.8s/%s/%s/request, "
             "SignedHeaders=content-type;host;x-content-sha256;x-date, Signature=%s",
             APP_ARK_AK, datebuf, ARK_REGION, ARK_SERVICE, sig_hex);
}

// ---------- HTTPS 请求 + 解析 ----------
bool ark_quota_fetch(int remain[3])
{
#if APP_ARK_ENABLE
    // 空密钥守卫：app_config.h 未填 AK 时静默跳过（模板固件保持兜底值，不打日志刷屏）
    if (APP_ARK_AK[0] == '\0') return false;

    // 签名需要可信时钟：RTC 兜底或 SNTP 校准后的时间都行，
    // 但 1970/2024 这类未初始化时间签出来必然 401，直接跳过等下轮
    time_t now = time(nullptr);
    if (now < 1735689600) {   // 2025-01-01 之前视为时钟未就绪
        ESP_LOGW(TAG, "clock not ready (%ld), skip", (long)now);
        return false;
    }

    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    char x_date[24];
    strftime(x_date, sizeof(x_date), "%Y%m%dT%H%M%SZ", &tm_utc);

    char body_hash[65];
    sha256_hex(ARK_BODY, strlen(ARK_BODY), body_hash);

    char auth[512];
    v4_sign(x_date, body_hash, auth);

    // ---- 发请求（证书 bundle 校验，与天气同款配置）----
    char url[160];
    snprintf(url, sizeof(url), "https://%s/?%s", ARK_HOST, ARK_QUERY);

    esp_http_client_config_t cfg = {};
    cfg.url               = url;
    cfg.method            = HTTP_METHOD_POST;
    cfg.timeout_ms        = 10000;
    cfg.buffer_size       = 4096;    // 响应头较长（Request-Id 等）
    cfg.buffer_size_tx    = 1024;    // 请求头含 ~250B Authorization 签名头；
    //                                 不设此项时 TX 固定 512B，会报
    //                                 "Buffer length is small to fit all the headers"（分批发，能通但吵）
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;

    esp_http_client_set_header(client, "Content-Type",     "application/json");
    esp_http_client_set_header(client, "X-Date",           x_date);
    esp_http_client_set_header(client, "X-Content-Sha256", body_hash);
    esp_http_client_set_header(client, "Authorization",    auth);

    std::string resp;
    bool http_ok = false;
    esp_err_t err = esp_http_client_open(client, (int)strlen(ARK_BODY));
    if (err == ESP_OK) {
        esp_http_client_write(client, ARK_BODY, (int)strlen(ARK_BODY));
        esp_http_client_fetch_headers(client);
        char buf[512];
        int n;
        while ((n = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
            resp.append(buf, n);
            if (resp.size() > 8 * 1024) break;   // 响应约 1KB，8KB 防御上限足够
        }
        http_ok = (esp_http_client_get_status_code(client) == 200) && !resp.empty();
        if (!http_ok) {
            ESP_LOGW(TAG, "HTTP %d, body: %.200s",
                     esp_http_client_get_status_code(client), resp.c_str());
        }
    } else {
        ESP_LOGE(TAG, "http open failed: %s", esp_err_to_name(err));
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (!http_ok) return false;

    // ---- 解析：Result.QuotaUsage[{Level, Percent(已用), ResetTimestamp}] ----
    cJSON *root = cJSON_Parse(resp.c_str());
    if (!root) {
        ESP_LOGW(TAG, "json parse failed");
        return false;
    }
    bool found[3] = { false, false, false };
    double used[3] = { 0, 0, 0 };
    cJSON *result = cJSON_GetObjectItem(root, "Result");
    cJSON *arr = result ? cJSON_GetObjectItem(result, "QuotaUsage") : NULL;
    if (cJSON_IsArray(arr)) {
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, arr) {
            cJSON *lv = cJSON_GetObjectItem(it, "Level");
            cJSON *pc = cJSON_GetObjectItem(it, "Percent");
            if (!cJSON_IsString(lv) || !cJSON_IsNumber(pc)) continue;
            int slot = -1;
            if      (strcmp(lv->valuestring, "session") == 0) slot = 0;  // 5 小时窗口
            else if (strcmp(lv->valuestring, "weekly") == 0)  slot = 1;  // 周
            else if (strcmp(lv->valuestring, "monthly") == 0) slot = 2;  // 月
            if (slot >= 0) {
                used[slot]  = pc->valuedouble;
                found[slot] = true;
            }
        }
    }
    cJSON_Delete(root);

    if (!(found[0] && found[1] && found[2])) {
        ESP_LOGW(TAG, "missing windows: s=%d w=%d m=%d（可能未订阅）", found[0], found[1], found[2]);
        return false;
    }

    // 接口给"已用"，屏幕显示"剩余"：四舍五入后夹到 0..100
    for (int i = 0; i < 3; i++) {
        int r = (int)(100.0 - used[i] + 0.5);
        remain[i] = r < 0 ? 0 : (r > 100 ? 100 : r);
    }
    ESP_LOGI(TAG, "quota ok: session used=%.1f%% remain=%d%%, weekly used=%.1f%% remain=%d%%, monthly used=%.1f%% remain=%d%%",
             used[0], remain[0], used[1], remain[1], used[2], remain[2]);
    return true;
#else
    (void)remain;
    return false;
#endif  // APP_ARK_ENABLE
}
