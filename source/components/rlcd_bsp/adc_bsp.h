// adc_bsp.h —— 电池电压/电量 ADC（移植自微雪官方例程，未改动）
#pragma once

#include <esp_adc/adc_oneshot.h>

void Adc_PortInit();
float Adc_GetBatteryVoltage();
uint8_t Adc_GetBatteryLevel();
