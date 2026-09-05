# Waveshare-RLCD4.2-DesktopInfoScreen

> **微雪（Waveshare）ESP32-S3-RLCD-4.2 桌面信息屏固件** —— 4.2 寸 ST7305 全反射屏（400×300，1-bit 黑白，断电可视、阳光下清晰）
>
> 基于 ESP-IDF v5.5 + LVGL v8.4 · 双页面按键切换 · 桌面/日历 · 天气 · 大模型额度显示

## ⚠️ 许可与使用限制（必读）

**本项目仅供个人学习、研究与自用，禁止任何形式的商用。**

- 不得出售、不得捆绑进付费产品/服务、不得用于商业部署或盈利目的
- 二次分发必须保留本声明与原仓库链接，并同样附加"禁止商用"条件
- 商用授权需联系作者获得书面许可

详见 [LICENSE](LICENSE)（中文为准，附英文摘要）。

## 功能一览

| 页面 | 内容 |
|---|---|
| **页面1：桌面** | 64px 大数字时钟（NTP + PCF85063 RTC 掉电兜底）、室内温湿度（SHTC3）、公开 API 天气（动态图标动画）、大模型剩余额度（5h/7d/30d 三周期进度条）、状态栏（日期/星期/WiFi/电量） |
| **页面2：日历** | 当月完整日历（星期头 + 6×7 网格），当天黑底高亮，跨天/跨月自动重绘 |

- **按键切页**：板上两颗可编程键（GPIO0 / GPIO18）左右切换，第三颗为电源键不可编程
- **TE 帧同步**：利用面板 TE 输出（GPIO6）同步整帧 GRAM 写入，实测面板自刷新 ~17Hz（58.4ms），
  同步后消除异步写入撕裂；配套把 LVGL 主循环最小延时 50ms→5ms（官方例程遗留，曾锁死动画帧率）
- **PC 像素级仿真器**：`tools/sim/sim.py` 无硬件即可预览所有页面的排版（含元素越界/重叠检测），
  固件坐标与仿真严格同步，任何布局改动先过仿真再上板

## 硬件

[微雪 ESP32-S3-RLCD-4.2](https://www.waveshare.net/)（4.2" 全反射 LCD 开发板）：

| 项目 | 值 |
|---|---|
| 屏幕 | ST7305 全反射屏 4.2"，LVGL 画布 400×300 横屏（1-bit 黑白） |
| 屏幕 SPI | MOSI=GPIO12, CLK=GPIO11, DC=GPIO5, CS=GPIO40, RST=GPIO41, **TE=GPIO6**，10MHz |
| 按键 | Key1=GPIO0（BOOT 丝印）/ Key4=GPIO18（KEY 丝印），低电平有效；Key3 接电源管理芯片不可编程 |
| I2C | SCL=GPIO14, SDA=GPIO13（SHTC3 温湿度 0x70 + PCF85063 RTC 0x51） |
| 电池 | ADC1_CH3（分压 ×3）+ PH1.0 可充电锂电座（RTC 掉电保持） |
| Flash/PSRAM | 16MB / 8MB Octal |

## 快速开始（免编译烧录）

`firmware/` 已备好三件套，用 esptool 直接烧：

```bash
pip install esptool
esptool.py --chip esp32s3 --port COMx --baud 460800 \
  --before default_reset --after hard_reset write_flash \
  0x0     firmware/bootloader_0x0.bin \
  0x8000  firmware/partition-table_0x8000.bin \
  0x10000 firmware/desktop_info_screen_0x10000.bin
```

> 烧录后屏幕能亮、时钟走（RTC 无电池则从 2020 起），但 WiFi / 天气 / 额度需要按下面自行编译配置。

## 自行编译

1. 安装 ESP-IDF **v5.5.x**（VS Code ESP-IDF 扩展或 EIM 离线包均可）
2. 打开 `source/` 目录，目标芯片选 **esp32s3**
3. 编辑 `source/main/app_config.h`（见下表，**先填 WiFi，密钥类按需**）
4. `idf.py -p COMx build flash monitor`

### 配置项（main/app_config.h）

| 配置 | 说明 |
|---|---|
| `APP_WIFI_SSID / PASSWORD` | WiFi 凭据（必填；ESP32-S3 只支持 2.4GHz） |
| `APP_WEATHER_PROVIDER` | 1=Open-Meteo（默认，免 key 开箱即用）；2=OpenWeather；3=和风天气 |
| `APP_WEATHER_LAT / LON` | Open-Meteo 经纬度 |
| `APP_ARK_ENABLE / APP_ARK_AK / APP_ARK_SK` | 火山方舟 Coding Plan 用量直连：AK/SK V4 签名调 `GetCodingPlanUsage`，10min 轮询，"已用%"自动换算"剩余%"。**密钥只填在本地，勿提交/分享；建议用只读 IAM 子用户密钥** |
| `APP_LLM_QUOTA_5H/7D/30D_PCT` | 大模型剩余额度手动兜底值（仅启动瞬间/接口长期失败时显示） |
| `APP_KEY_PREV_IO / NEXT_IO` | 左右切页按键 GPIO（默认 0/18，方向不顺手可对调） |
| `APP_SENSOR_PERIOD_S` 等 | 刷新周期（默认：传感器 5s / 电量 60s / 天气 30min / UI 1s / 额度 10min） |

> 安全提示：**任何密钥（WiFi 密码、AK/SK、天气 API key）都不应提交到公开仓库**。
> 本仓库中的 `app_config.h` 已全部置空，请本地填写。

## 刷新频率

| 数据 | 周期 | 实现 |
|---|---|---|
| 时钟显示 | 1s | lv_timer（LVGL 任务内） |
| SHTC3 温湿度 | 5s | sensor_task |
| 电池电量 | 60s | sensor_task |
| 天气 | 30min（失败 5min 重试） | net_task + esp_http_client |
| 大模型用量 | 10min（失败 2min 重试） | net_task → `main/quota/ark_quota.cpp`（V4 签名直连火山 OpenAPI） |
| SNTP / RTC 回写 | 一次性 + 6h | time_task |

## 中文字库（按需子集）

界面中文用 `main/ui/font_chinese_16.c`（黑体 16px、bpp=1，只裁剪界面实际出现的字），
数字用 `font_num_64/48/28.c`（Arial Bold，bpp=1）。生成脚本见 `tools/font/gen_font_chinese16.ps1`，
新增汉字后按脚本内注释重新生成（注意：入口必须是包根 `lv_font_conv.js`，`lib/cli.js` 没有 main 入口，
直跑会"假成功"）。

## PC 布局仿真器

```bash
cd tools/sim
python sim.py     # 输出六场景 ASCII 渲染 + PNG（tools/sim/out/）+ 越界/重叠自检
```

覆盖桌面（常规/极值/无数据）与日历（常规/6 行满格/时钟未就绪）六种场景。
`png1bit.py` 可把渲染结果镜像到本地文件夹查看。

## 已知取舍

- 面板自刷新 ~17Hz：滑动动画帧间跳跃感无法消除（已实测），故页面切换采用瞬切；
  TE 帧同步保留在 `display_bsp.cpp`，所有常规刷新无撕裂
- `full_refresh` 每帧整屏重写（官方例程同款），1s 级刷新毫无压力
- 1-bit 屏无灰阶；LVGL 主题的彩色/圆角元素已强制黑白直角
- 中文为子集字库：界面出现的字都在，任意外来中文可能缺字（默认 Open-Meteo 文案完全可控）

## 目录结构

```
├── README.md / LICENSE
├── firmware/                 免编译烧录三件套（偏移见文件名）
└── source/
    ├── CMakeLists.txt / sdkconfig.defaults / partitions.csv
    ├── components/
    │   ├── rlcd_bsp/         硬件层：display_bsp(ST7305+TE帧同步)、i2c、SHTC3/RTC、ADC
    │   └── rlcd_app/         中间层：lvgl_bsp、wifi_sta_bsp
    ├── main/
    │   ├── app_config.h      ⭐ 全部配置集中在此
    │   ├── main.cpp          入口 + LVGL flush（RGB565→1-bit 二值化）
    │   ├── ui/               桌面/日历双页面 + 子集字库 + 天气图标
    │   ├── data/             data_store(共享仓库) + data_tasks(sensor/time/net)
    │   ├── quota/            火山方舟 Coding Plan 用量直查（V4 签名）
    │   └── key/              板载按键扫描（消抖 + 原子事件槽）
    └── tools/
        ├── sim/              PC 像素级布局仿真器
        └── font/             字库生成脚本
```

## License

仅供个人学习/研究/自用，**禁止任何商用** —— 见 [LICENSE](LICENSE)。

Copyright (c) 2026 @hemo528
