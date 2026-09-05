// data_tasks.h —— 数据采集任务的启动入口
#pragma once

// 初始化 I2C/RTC/SHTC3/ADC（RTC 时间回灌系统时钟）并创建三个采集任务：
//   sensor_task : SHTC3 温湿度 + 电池电量
//   time_task   : 系统时间缓存 + SNTP 校准 + RTC 周期回写
//   net_task    : 天气 API + 大模型用量接口（可选）
// 全部任务不直接触碰 LVGL，只写 data_store。
void data_tasks_start(void);
