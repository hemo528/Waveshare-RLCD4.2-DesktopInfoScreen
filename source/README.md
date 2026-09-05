# DesktopInfoScreen —— ESP32-S3-RLCD-4.2 桌面信息屏

基于 **ESP-IDF (v5.5+) + LVGL v8.4** 的桌面信息屏：室内温湿度、NTP 同步时钟（PCF85063 RTC 掉电兜底）、
公开 API 天气、大模型剩余额度（5h / 7d / 30d 三周期）、电池电量与 WiFi 状态。

硬件参数全部取自微雪官方例程实测值（见 `components/rlcd_bsp`，与 `10_FactoryProgram` 同源）：

| 项目 | 值 |
|---|---|
| 屏幕 | ST7305 全反射屏 4.2"，LVGL 画布 400×300 横屏（1-bit 黑白） |
| 屏幕 SPI | MOSI=GPIO12, CLK=GPIO11, DC=GPIO5, CS=GPIO40, RST=GPIO41, 10MHz |
| I2C 总线 | SCL=GPIO14, SDA=GPIO13 (port0, 400kHz) |
| 传感器 | SHTC3 温湿度（地址 0x70）；PCF85063 RTC（地址 0x51） |
| 电池 ADC | ADC1_CH3（分压 ×3） |
| Flash/PSRAM | 16MB / 8MB Octal（sdkconfig.defaults 已配好） |

## 目录结构

```
DesktopInfoScreen/
├── CMakeLists.txt / sdkconfig.defaults / partitions.csv
├── components/
│   ├── rlcd_bsp/    硬件层：display_bsp(ST7305)、i2c_bsp、i2c_equipment(SHTC3+RTC)、
│   │                rtc_pcf85063(自写最小驱动，替代SensorLib)、adc_bsp(电池)
│   └── rlcd_app/    中间层：lvgl_bsp(官方LVGL v8移植)、wifi_sta_bsp(精简WiFi STA)
└── main/
    ├── app_config.h        ⭐ 全部配置集中在此（WiFi/城市/API key/刷新周期/额度初值）
    ├── main.cpp            入口 + LVGL flush 回调（RGB565→1-bit 二值化）
    ├── ui/ui_desktop.cpp   布局 + 1s ui_tick 刷新
    └── data/               data_store(共享仓库+互斥锁) + data_tasks(sensor/time/net 三任务)
```

## 编译烧录（VS Code + ESP-IDF 扩展）

1. 用 EIM 离线包装好 ESP-IDF v5.5.x（`开发工具/archive_v5.5.5_windows-x64.zst`，EIM 选「从存档安装」）。
2. VS Code 打开 `DesktopInfoScreen` 文件夹 → `ESP-IDF: Select Espressif Device Target` 选 **esp32s3**。
3. 设置串口 → `ESP-IDF: Build`（首次会经 Component Manager 拉取 lvgl 8.4.x）→ `Flash (UART)` + `Monitor`。
   命令行等价：`idf.py -p COMx build flash monitor`
4. 烧录前编辑 `main/app_config.h`：WiFi 名称/密码、城市与坐标（见下）。

> sdkconfig 无需手工配置：首次构建会自动套用 `sdkconfig.defaults`（PSRAM/OPI、16MB Flash、LVGL 字体等）。
> 若之前生成过 sdkconfig，改 defaults 后请删除 sdkconfig 重新构建。

## 配置项（main/app_config.h）

| 配置 | 说明 |
|---|---|
| `APP_WIFI_SSID / PASSWORD` | WiFi 凭据（必填） |
| `APP_WEATHER_PROVIDER` | 1=Open-Meteo（默认，免 key 开箱即用）；2=OpenWeather；3=和风天气 |
| `APP_WEATHER_LAT / LON` | Open-Meteo 经纬度（默认杭州） |
| `APP_WEATHER_OWM_KEY / QW_KEY` | 对应 API key；和风还需 `QW_LOC` 城市 ID |
| `APP_ARK_ENABLE / APP_ARK_AK / APP_ARK_SK` | 火山方舟 Coding Plan 用量直连：AK/SK V4 签名调 `GetCodingPlanUsage`（OpenTOP），10min 轮询，接口"已用%"自动换算"剩余%" |
| `APP_LLM_QUOTA_5H/7D/30D_PCT` | 大模型剩余额度手动兜底值（仅启动瞬间 / API 长期失败时显示） |
| `APP_SENSOR_PERIOD_S` 等 | 刷新周期（默认：传感器 5s / 电量 60s / 天气 30min / UI 1s） |

## 时钟策略（重要）

1. 上电：读 PCF85063 回灌系统时钟 → 没网也有合理时间；
2. WiFi 通后启动 SNTP（阿里云 NTP）→ 校准成功立即回写 RTC，之后每 6h 补写；
3. 屏幕上 `NTP synced` / `RTC time` 标识当前时钟来源。
   注意：RTC 需要**可充电 PH1.0 电池**（如 LIR2032）装在独立电池座上才有掉电保持。

## 刷新频率一览

| 数据 | 周期 | 实现 |
|---|---|---|
| 时钟显示 | 1s | lv_timer（LVGL 任务内） |
| SHTC3 温湿度 | 5s | FreeRTOS sensor_task（唤醒→测量→睡眠） |
| 电池电量 | 60s | 同上 |
| 天气 | 30min（失败 5min 重试） | FreeRTOS net_task + esp_http_client |
| 大模型用量 | 10min（失败 2min 重试） | FreeRTOS net_task → `main/quota/ark_quota.cpp`（V4 签名直连火山 OpenAPI） |
| SNTP / RTC 回写 | 一次性 + 6h | time_task |

## 中文字库（按需子集，不是整包 CJK）

界面中文用 `main/ui/font_chinese_16.c`（约 35KB，黑体 16px、bpp=1 匹配 1-bit 屏），
由 `lv_font_conv` 只裁剪**界面实际出现的字**生成（星期/室内/天气词/电量/用量等约 70 字 + ASCII）。
字库数据是 const，放 Flash 不占 RAM。

新增汉字（如换城市名、加天气词）后重新生成：

```powershell
# 一次性：把 lv_font_conv 装到本地（走 npmmirror 加速）
npm install --prefix C:\Espressif\tools\lvfont lv_font_conv

# 每次改字集后执行：--symbols 里追加新字即可（整条一行；入口必须用包根 lv_font_conv.js，
# lib/cli.js 没有 main 入口、直跑会"假成功"；建议直接跑 tools/font/gen_font_chinese16.ps1）
C:\Program Files\nodejs\node.exe C:\Espressif\tools\lvfont\node_modules\lv_font_conv\lv_font_conv.js `
  --no-compress --bpp 1 --size 16 --font C:\Windows\Fonts\simhei.ttf `
  -r 0x20-0x7E `
  --symbols "·°星期一二三四五六日室内温湿度外天气晴多云阴雾小中大雨暴雷阵雪夹沙尘伴冰雹强轻冻烟霾浮更新无数据大模型用量剩余小时天周期已同步电量南京年月" `
  --format lvgl --lv-font-name chinese_16 `
  -o C:\Espressif\projects\DesktopInfoScreen\main\ui\font_chinese_16.c
```

注意：
- 必须用 `node.exe` 直接跑 `lib\cli.js`——走 `npx` 时批处理层会把 CJK 参数按 GBK 编码损坏，
  生成乱码字形（从生成文件头部的 Opts 注释可检查是否乱码）。
- `wmo_text()`（data_tasks.cpp）返回的每个词的字都必须在 `--symbols` 里。
- main/CMakeLists.txt 里已为字库文件定义 `LV_LVGL_H_INCLUDE_SIMPLE`。

## LVGL 模拟器先行验证 UI（可选）

`ui_desktop.cpp` 只依赖 LVGL API + `data_store`，移植到 PC 模拟器只需提供数据桩：

1. 用 VS Code 装 **ESP-IDF EDP** 或直接用 [lv_port_pc_vscode（SDL2）](https://github.com/lvgl/lv_port_pc_vscode)（LVGL v8 分支）。
2. 把 `main/ui/ui_desktop.cpp`、`main/data/data_store.{h,cpp}` 拷入工程；模拟器版 `main()` 调
   `data_store_init()` + 填几组假数据（`data_set_weather(...)` 等）+ `ui_desktop_create()` 即可，
   其中 `wifi_sta_ip_str()` 在模拟器桩里返回 `"127.0.0.1"`。
3. 模拟器分辨率设为 400×300、色深 16bit —— 布局/字号/进度条即可在 PC 上预览调好，再上板。

## 已知取舍（先可运行，后优化）

- 官方 LVGL port 为 `full_refresh` + 每帧全屏逐像素 LUT 写入（1s 一帧毫无压力）；
  若日后做动画，可改成脏矩形累加 + 局部 `RLCD_Display` 扩展，降低 CPU。
- 反射屏 1-bit：未做灰阶抖动；LVGL 默认主题的彩色元素（如 lv_bar 蓝色）已强制黑白。
- 中文为**子集字库**（见"中文字库"一节）：界面出现的字都在，但任意外来中文（如
  OpenWeather 返回的英文/和风返回的生僻词）可能缺字；默认 Open-Meteo 的文案完全可控。
- `data_store` 用一把全局互斥锁，临界区极短（拷贝结构体），对 1s 级 UI 完全够用。

## 待确认项清单

- [x] WiFi：`你的WiFi名称` / 你的WiFi密码 ⚠️ 需确认该 SSID 覆盖 **2.4GHz**（ESP32 不支持 5GHz 频段）
- [x] 城市：南京（32.06N / 118.79E，UI 显示 Nanjing）
- [x] 天气：Open-Meteo（免 key，开箱即用）
- [x] 大模型：火山引擎 Coding Plan——ESP32 直连官方 `GetCodingPlanUsage`（AK/SK V4 签名，2026-09 实测），三窗口剩余量 10min 自动刷新
- [x] 时区：CST-8（东八区）
- [ ] RTC 备用电池（PH1.0 可充电 LIR2032）是否安装（影响断电后时间保持）
