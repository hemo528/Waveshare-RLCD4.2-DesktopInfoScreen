// data_store.cpp —— 共享数据仓库实现（FreeRTOS 互斥锁保护）
#include "data_store.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

data_store_t g_data;

static SemaphoreHandle_t s_mutex = NULL;

void data_store_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
}

void data_lock(void)   { xSemaphoreTake(s_mutex, portMAX_DELAY); }
void data_unlock(void) { xSemaphoreGive(s_mutex); }

void data_set_indoor(float t, float rh, bool ok)
{
    data_lock();
    g_data.indoor_temp_c = t;
    g_data.indoor_rh     = rh;
    g_data.indoor_ok     = ok;
    data_unlock();
}

void data_set_battery(int pct)
{
    data_lock();
    g_data.batt_pct = pct;
    data_unlock();
}

void data_set_weather(const char *text, int icon, float temp, int rh, int source)
{
    data_lock();
    snprintf(g_data.wx_text, sizeof(g_data.wx_text), "%s", text);
    g_data.wx_icon   = icon;
    g_data.wx_temp_c = temp;
    g_data.wx_rh     = rh;
    g_data.wx_ok     = true;
    g_data.wx_source = source;
    time(&g_data.wx_ts);
    data_unlock();
}

void data_set_quota(int slot5h, int slot7d, int slot30d, const char *src)
{
    data_lock();
    g_data.quota_pct[0] = slot5h;
    g_data.quota_pct[1] = slot7d;
    g_data.quota_pct[2] = slot30d;
    snprintf(g_data.quota_src, sizeof(g_data.quota_src), "%s", src);
    data_unlock();
}
