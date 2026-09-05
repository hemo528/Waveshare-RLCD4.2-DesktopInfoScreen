// rtc_pcf85063.h —— PCF85063 RTC 最小驱动（替代 SensorLib，接口仅保留本工程所需）
//
// 寄存器映射（BCD 编码，24 小时制）：
//   0x00 Ctrl_1   0x01 Ctrl_2
//   0x02 Seconds  (bit7 = OS 标志)
//   0x03 Minutes
//   0x04 Hours    (24h 模式 bit7=0)
//   0x05 Days
//   0x06 Weekdays (bit2..0，0=Sunday)
//   0x07 Months   (bit7 = century 标志)
//   0x08 Years    (00..99，按 2000 + 值解释)
#pragma once

#include <cstddef>
#include <cstdint>

class Pcf85063Drv {
public:
    // IO 抽象：write=true 从 reg 开始写 len 字节；否则从 reg 开始读 len 字节
    typedef bool (*io_t)(void *user, uint8_t reg, uint8_t *buf, size_t len, bool is_write);

    // 探测芯片是否应答（读一次控制寄存器）
    bool begin(io_t io, void *user);

    // 写入日期时间；weekday 由 y/m/d 计算得出（0=Sunday）
    bool setDateTime(int year, int month, int day, int hour, int minute, int second);

    // 读取日期时间；wday: 0=Sunday..6=Saturday
    bool getDateTime(int *year, int *month, int *day,
                     int *hour, int *minute, int *second, int *wday);

private:
    io_t  io_   = nullptr;
    void *user_ = nullptr;

    bool readRegs(uint8_t reg, uint8_t *buf, size_t len);
    bool writeRegs(uint8_t reg, const uint8_t *buf, size_t len);

    static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
    static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
    static int     day_of_week(int y, int m, int d);  // Sakamoto 算法，0=Sunday
};
