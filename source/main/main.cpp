// main.cpp —— 桌面信息屏入口
//
// 初始化顺序：
//   1. 屏幕驱动（ST7305 初始化，全局对象构造时已建好 SPI 总线）
//   2. LVGL 移植层（双 PSRAM 缓冲 + tick 定时器 + LVGL 任务）
//   3. 持锁创建 UI（含 1s ui_tick 定时器）
//   4. 数据任务（I2C/RTC/SHTC3/ADC + sensor/time/net 三任务）
//   5. WiFi（异步连接，连接成功后 SNTP/天气自动跟进）
#include <stdio.h>

#include "display_bsp.h"
#include "lvgl_bsp.h"
#include "wifi_sta_bsp.h"
#include "app_config.h"
#include "data_tasks.h"
#include "data_store.h"
#include "ui/ui_desktop.h"

// 屏幕引脚：MOSI=12, CLK=11, DC=5, CS=40, RST=41, TE=6；横屏 400×300
// （引脚来自官方例程 10_FactoryProgram main.cpp，与原理图一致；TE 用于帧同步写 GRAM）
DisplayPort RlcdPort(APP_DISP_MOSI, APP_DISP_CLK, APP_DISP_DC, APP_DISP_CS, APP_DISP_RST, 400, 300, SPI3_HOST, APP_DISP_TE);

// LVGL → ST7305 的桥接：
// 反射屏只有 1-bit 显示，这里把 LVGL 的 RGB565 缓冲按阈值二值化后逐像素写入驱动帧缓冲，
// 最后 RLCD_Display() 把整帧 15KB 通过 SPI 推给屏幕（与官方例程一致的实现）。
static void Lvgl_FlushCallback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    uint16_t *buffer = (uint16_t *)color_map;
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            uint8_t color = (*buffer < 0x7fff) ? ColorBlack : ColorWhite;
            RlcdPort.RLCD_SetPixel(x, y, color);
            buffer++;
        }
    }
    RlcdPort.RLCD_Display();
    lv_disp_flush_ready(drv);
}

extern "C" void app_main(void)
{
    // 0. 数据仓库最先初始化（ui_desktop_create 里的 ui_tick 立即刷新会用到它的互斥锁）
    data_store_init();

    // 1. 屏幕初始化（ST7305 上电序列，约 200ms）
    RlcdPort.RLCD_Init();

    // 2. LVGL 移植层
    Lvgl_PortInit(400, 300, Lvgl_FlushCallback);

    // 3. UI（持锁创建；ui_tick 定时器在 LVGL 任务内运行）
    if (Lvgl_lock(-1)) {
        ui_desktop_create();
        Lvgl_unlock();
    }

    // 4. 数据采集任务（sensor/time/net，写 data_store，不碰 LVGL）
    data_tasks_start();

    // 5. WiFi（非阻塞；连上后 time_task 启动 SNTP、net_task 拉天气）
    wifi_sta_start(APP_WIFI_SSID, APP_WIFI_PASSWORD);
}
