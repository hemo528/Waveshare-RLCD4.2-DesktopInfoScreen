// rtc_pcf85063.cpp —— PCF85063 RTC 最小驱动实现
#include "rtc_pcf85063.h"
#include <esp_log.h>

#define RTC_TAG "PCF85063"

#define PCF85063_REG_CTRL1   0x00
#define PCF85063_REG_SECONDS 0x02   // sec,min,hour,day,wday,month,year = 0x02..0x08

bool Pcf85063Drv::begin(io_t io, void *user) {
    io_   = io;
    user_ = user;
    uint8_t ctrl = 0;
    if (!readRegs(PCF85063_REG_CTRL1, &ctrl, 1)) {
        return false;
    }
    // 保险起见：NORMAL mode(12h/24h 默认 24h)、无中断输出，只清软件复位相关位
    return true;
}

bool Pcf85063Drv::readRegs(uint8_t reg, uint8_t *buf, size_t len) {
    return io_ != nullptr && io_(user_, reg, buf, len, false);
}

bool Pcf85063Drv::writeRegs(uint8_t reg, const uint8_t *buf, size_t len) {
    return io_ != nullptr && io_(user_, reg, const_cast<uint8_t *>(buf), len, true);
}

int Pcf85063Drv::day_of_week(int y, int m, int d) {
    // Sakamoto 算法，返回 0=Sunday..6=Saturday
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

bool Pcf85063Drv::setDateTime(int year, int month, int day,
                              int hour, int minute, int second) {
    if (year < 2000) year = 2000;
    uint8_t yy  = (uint8_t)(year - 2000);
    uint8_t wd  = (uint8_t)day_of_week(year, month, day);

    uint8_t buf[7];
    buf[0] = bin2bcd((uint8_t)(second & 0x7F));   // 清 OS 位
    buf[1] = bin2bcd((uint8_t)minute);
    buf[2] = bin2bcd((uint8_t)hour);              // 24h 模式（bit7=0）
    buf[3] = bin2bcd((uint8_t)day);
    buf[4] = (uint8_t)(wd & 0x07);
    buf[5] = bin2bcd((uint8_t)month);             // century 位 = 0
    buf[6] = bin2bcd(yy);

    return writeRegs(PCF85063_REG_SECONDS, buf, 7);
}

bool Pcf85063Drv::getDateTime(int *year, int *month, int *day,
                              int *hour, int *minute, int *second, int *wday) {
    uint8_t buf[7];
    if (!readRegs(PCF85063_REG_SECONDS, buf, 7)) {
        return false;
    }
    if (second) *second = bcd2bin((uint8_t)(buf[0] & 0x7F));
    if (minute) *minute = bcd2bin((uint8_t)(buf[1] & 0x7F));
    if (hour)   *hour   = bcd2bin((uint8_t)(buf[2] & 0x3F));
    if (day)    *day    = bcd2bin((uint8_t)(buf[3] & 0x3F));
    if (wday)   *wday   = (int)(buf[4] & 0x07);
    if (month)  *month  = bcd2bin((uint8_t)(buf[5] & 0x1F));
    if (year)   *year   = 2000 + bcd2bin(buf[6]);
    return true;
}
