# 硬件层组件：屏幕(ST7305) / I2C 总线 / SHTC3 + PCF85063 / 电池 ADC
# 移植自微雪官方例程 10_FactoryProgram 的 port_bsp，去掉了本工程用不到的
# SD 卡 / 音频 codec / 按键 / SensorLib（RTC 改用自写的 rtc_pcf85063 最小驱动）。
idf_component_register(
    SRCS
    "display_bsp.cpp"
    "i2c_bsp.cpp"
    "i2c_equipment.cpp"
    "rtc_pcf85063.cpp"
    "adc_bsp.cpp"
    INCLUDE_DIRS
    "./"
    REQUIRES
    driver
    esp_lcd
    esp_adc
    esp_timer
)
