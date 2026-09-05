// display_bsp.cpp —— ST7305 全反射屏驱动（移植自微雪官方例程 10_FactoryProgram，
// 仅删除了文件末尾 #if 0 包裹的死代码，其余逻辑与官方一致）
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_rom_sys.h>
#include "display_bsp.h"

DisplayPort::DisplayPort(int mosi, int scl, int dc, int cs, int rst, int width, int height, spi_host_device_t spihost, int te_pin) :
mosi_(mosi),
scl_(scl),
dc_(dc),
cs_(cs),
rst_(rst),
width_(width),
height_(height),
te_pin_(te_pin)
{
    esp_err_t        ret;
    spi_bus_config_t buscfg   = {};
    int              transfer = width_ * height_;
    buscfg.miso_io_num                   = -1;
    buscfg.mosi_io_num                   = mosi;
    buscfg.sclk_io_num                   = scl;
    buscfg.quadwp_io_num                 = -1;
    buscfg.quadhd_io_num                 = -1;
    buscfg.max_transfer_sz               = transfer;
    ret                                  = spi_bus_initialize(spihost, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.dc_gpio_num = dc_;
    io_config.cs_gpio_num = cs_;
    io_config.pclk_hz = 10 * 1000 * 1000;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.spi_mode = 0;
    io_config.trans_queue_depth = 10;

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)spihost, &io_config, &io_handle));

    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type     = GPIO_INTR_DISABLE;
    gpio_conf.mode          = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask  = (0x1ULL << rst_);
    gpio_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en    = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    // TE 引脚：面板 0x35 0x00 已开启 Tearing Effect 输出，这里把它配成输入用于帧同步
    if (te_pin_ >= 0) {
        gpio_config_t tecfg     = {};
        tecfg.intr_type         = GPIO_INTR_DISABLE;
        tecfg.mode              = GPIO_MODE_INPUT;
        tecfg.pin_bit_mask      = (0x1ULL << te_pin_);
        tecfg.pull_down_en      = GPIO_PULLDOWN_DISABLE;
        tecfg.pull_up_en        = GPIO_PULLUP_ENABLE;
        ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&tecfg));
    }

    Set_ResetIOLevel(1);

    DisplayLen                = transfer >> 3; //(1byte 8ipex)
    DispBuffer                = (uint8_t *) heap_caps_malloc(DisplayLen, MALLOC_CAP_SPIRAM);
    assert(DispBuffer);

#if (AlgorithmOptimization == 3)
	PixelIndexLUT = (uint16_t (*)[300])heap_caps_malloc(transfer * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
	PixelBitLUT   = (uint8_t (*)[300])heap_caps_malloc(transfer * sizeof(uint8_t), MALLOC_CAP_SPIRAM);
    assert(PixelIndexLUT);
    assert(PixelBitLUT);
    if(width_ == 400) {
        InitLandscapeLUT();
    } else {
        InitPortraitLUT();
    }
#endif
}

DisplayPort::~DisplayPort() {
}

void DisplayPort::RLCD_Init() {
    RLCD_Reset();

    RLCD_SendCommand(0xD6);  // NVM Load Control
	RLCD_SendData(0x17);
	RLCD_SendData(0x02);

	RLCD_SendCommand(0xD1); //Booster Enable
	RLCD_SendData(0x01);

	RLCD_SendCommand(0xC0); //Gate Voltage Control
	RLCD_SendData(0x11);
	RLCD_SendData(0x04);

	RLCD_SendCommand(0xC1); //VSHP Setting
	RLCD_SendData(0x41);
	RLCD_SendData(0x41);
	RLCD_SendData(0x41);
	RLCD_SendData(0x41);

	RLCD_SendCommand(0xC2);
	RLCD_SendData(0x19);
	RLCD_SendData(0x19);
	RLCD_SendData(0x19);
	RLCD_SendData(0x19);

	RLCD_SendCommand(0xC4);
	RLCD_SendData(0x41);
	RLCD_SendData(0x41);
	RLCD_SendData(0x41);
	RLCD_SendData(0x41);

	RLCD_SendCommand(0xC5);
	RLCD_SendData(0x19);
	RLCD_SendData(0x19);
	RLCD_SendData(0x19);
	RLCD_SendData(0x19);

	RLCD_SendCommand(0xD8);
	RLCD_SendData(0xA6);
	RLCD_SendData(0xE9);

	RLCD_SendCommand(0xB2);
	RLCD_SendData(0x05);

	RLCD_SendCommand(0xB3);
	RLCD_SendData(0xE5);
	RLCD_SendData(0xF6);
	RLCD_SendData(0x05);
	RLCD_SendData(0x46);
	RLCD_SendData(0x77);
	RLCD_SendData(0x77);
	RLCD_SendData(0x77);
	RLCD_SendData(0x77);
	RLCD_SendData(0x76);
	RLCD_SendData(0x45);

	RLCD_SendCommand(0xB4);
	RLCD_SendData(0x05);
	RLCD_SendData(0x46);
	RLCD_SendData(0x77);
	RLCD_SendData(0x77);
	RLCD_SendData(0x77);
	RLCD_SendData(0x77);
	RLCD_SendData(0x76);
	RLCD_SendData(0x45);

	RLCD_SendCommand(0x62);
	RLCD_SendData(0x32);
	RLCD_SendData(0x03);
	RLCD_SendData(0x1F);

	RLCD_SendCommand(0xB7);
	RLCD_SendData(0x13);

	RLCD_SendCommand(0xB0);
	RLCD_SendData(0x64);

	RLCD_SendCommand(0x11);
	vTaskDelay(pdMS_TO_TICKS(200));
	RLCD_SendCommand(0xC9);
	RLCD_SendData(0x00);

	RLCD_SendCommand(0x36);
	RLCD_SendData(0x48);

	RLCD_SendCommand(0x3A);
	RLCD_SendData(0x11);

	RLCD_SendCommand(0xB9);
	RLCD_SendData(0x20);

	RLCD_SendCommand(0xB8);
	RLCD_SendData(0x29);

	RLCD_SendCommand(0x21);

	RLCD_SendCommand(0x2A);
	RLCD_SendData(0x12);
	RLCD_SendData(0x2A);

	RLCD_SendCommand(0x2B);
	RLCD_SendData(0x00);
	RLCD_SendData(0xC7);

	RLCD_SendCommand(0x35);
	RLCD_SendData(0x00);

	RLCD_SendCommand(0xD0);
	RLCD_SendData(0xFF);

	RLCD_SendCommand(0x38);
	RLCD_SendCommand(0x29);

    RLCD_ColorClear(ColorWhite);

    // TE 同步自检：量面板自刷新帧周期（两次 TE 上升沿间隔），确认同步通路可用。
    // 整帧写入 ≈15ms，只要帧周期明显大于写入时间，同步后就不会撕裂
    if (te_pin_ >= 0) {
        if (RLCD_WaitTE(50)) {
            int64_t t0  = esp_timer_get_time();
            int64_t t1  = 0;
            int     got = 0;
            while (got < 2 && esp_timer_get_time() - t0 < 200000) {
                if (RLCD_WaitTE(60)) {
                    if (got == 0) t0 = esp_timer_get_time();
                    else          t1 = esp_timer_get_time();
                    got++;
                }
            }
            if (got == 2)
                ESP_LOGI(TAG, "TE sync on: panel frame period ~%lld us", (t1 - t0));
            else
                ESP_LOGW(TAG, "TE pulses caught=%d, sync may be unreliable", got);
        } else {
            ESP_LOGW(TAG, "TE pulse not seen, writes will run unsynced");
        }
    }
}

void DisplayPort::RLCD_ColorClear(uint8_t color) {
    memset(DispBuffer, color, DisplayLen);
}

// 等面板 TE（Tearing Effect）帧起始脉冲：0x35 0x00 模式下 TE 在垂直消隐开始处产生上升沿。
// 在 TE 边沿后立刻整帧写 GRAM，写入指针始终跑在面板自扫描前面 → 从根源消除撕裂。
// 忙等采样 20µs（TE 脉宽为行周期级，足够捕捉）；超时返回 false，调用方照常写帧（降级为不同步）
bool DisplayPort::RLCD_WaitTE(uint32_t timeout_ms) {
    if (te_pin_ < 0) return false;
    int64_t t0  = esp_timer_get_time();
    int     last = gpio_get_level((gpio_num_t)te_pin_);
    while (esp_timer_get_time() - t0 < (int64_t)timeout_ms * 1000) {
        int now = gpio_get_level((gpio_num_t)te_pin_);
        if (last == 0 && now == 1) return true;   // 捕捉到上升沿 = 新帧开始
        last = now;
        esp_rom_delay_us(20);
    }
    return false;
}

void DisplayPort::RLCD_Display() {
    // 帧同步：等 TE 边沿再动 GRAM（未配置 TE 引脚或超时时直接写，行为同旧版）
    RLCD_WaitTE(30);

    RLCD_SendCommand(0x2A);     // Column Address Set
  	RLCD_SendData(0x12);
  	RLCD_SendData(0x2A);

  	RLCD_SendCommand(0x2B);     // Page Address Set
  	RLCD_SendData(0x00);
  	RLCD_SendData(0xC7);

  	RLCD_SendCommand(0x2c);     // Page Address Set

	RLCD_Sendbuffera(DispBuffer,DisplayLen);
}

void DisplayPort::RLCD_Reset(void) {
    Set_ResetIOLevel(1);
    vTaskDelay(pdMS_TO_TICKS(50));
    Set_ResetIOLevel(0);
    vTaskDelay(pdMS_TO_TICKS(20));
    Set_ResetIOLevel(1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

void DisplayPort::RLCD_SendCommand(uint8_t Reg) {
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle, Reg, NULL, 0));
}

void DisplayPort::RLCD_SendData(uint8_t Data) {
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle, -1, &Data, 1));
}

void DisplayPort::RLCD_Sendbuffera(uint8_t *Data, int len) {
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(io_handle, -1, Data, len));
}

void DisplayPort::Set_ResetIOLevel(uint8_t level) {
    gpio_set_level((gpio_num_t) rst_, level ? 1 : 0);
}
#if (AlgorithmOptimization != 3)

void DisplayPort::RLCD_SetPortraitPixel(uint16_t x, uint16_t y, uint8_t color) {
    if((x >= width_) || (y >= height_)) {
  	  	ESP_LOGE("Pixel","Beyond the limit : (%d,%d)",x ,y);
        return;
  	}
#if (AlgorithmOptimization == 2)
	const uint16_t W4 = width_ >> 2;

    uint16_t byte_x = x >> 2;
    uint16_t byte_y = y >> 1;

    uint32_t index = byte_y * W4 + byte_x;

    uint8_t local_x = x & 0x03;
    uint8_t local_y = y & 0x01;

    uint8_t bit = 7 - ((local_x << 1) | local_y);

    uint8_t mask = 1 << bit;

    if (color)
        DispBuffer[index] |= mask;
    else
        DispBuffer[index] &= ~mask;
#else
    uint16_t byte_x = x / 4;
    uint16_t byte_y = y / 2;

    uint32_t index = byte_y * (width_ / 4) + byte_x;

    uint8_t local_x = x % 4;
    uint8_t local_y = y % 2;
    uint8_t bit = 7 - (local_x * 2 + local_y);
    if (color)
        DispBuffer[index] |=  (1 << bit);
    else
        DispBuffer[index] &= ~(1 << bit);
#endif
}

void DisplayPort::RLCD_SetLandscapePixel(uint16_t x, uint16_t y, uint8_t color) {
    if (x >= width_ || y >= height_)
        return;
#if (AlgorithmOptimization == 2)

	uint16_t inv_y = (height_ - 1 - y);
    const uint16_t H4 = height_ >> 2;
    uint16_t byte_x = x >> 1;
    uint16_t block_y = inv_y >> 2;
    uint32_t index = byte_x * H4 + block_y;
    uint8_t local_x = x & 0x01;
    uint8_t local_y = inv_y & 0x03;
    uint8_t bit = 7 - ((local_y << 1) | local_x);
    uint8_t mask = 1 << bit;
    if (color)
        DispBuffer[index] |= mask;
    else
        DispBuffer[index] &= ~mask;
#else
    uint16_t inv_y = height_ - 1 - y;

    uint16_t byte_x  = x / 2;           // 0..199
    uint16_t block_y = inv_y / 4;       // 0..74

    uint32_t index = byte_x * (height_ / 4) + block_y;

    uint8_t local_x = x % 2;            // 0 or 1
    uint8_t local_y = inv_y % 4;        // 0..3

    uint8_t bit = 7 - (local_y * 2 + local_x);

    if (color)
        DispBuffer[index] |= (1 << bit);
    else
        DispBuffer[index] &= ~(1 << bit);
#endif
}

#endif


#if (AlgorithmOptimization == 3)

void DisplayPort::InitPortraitLUT() {
    uint16_t W4 = width_ >> 2;
    for (uint16_t y = 0; y < height_; y++)
    {
        uint16_t byte_y = y >> 1;
        uint8_t  local_y = y & 1;

        for (uint16_t x = 0; x < width_; x++)
        {
            uint16_t byte_x = x >> 2;
            uint8_t  local_x = x & 3;

            uint32_t index = byte_y * W4 + byte_x;
            uint8_t bit = 7 - ((local_x << 1) | local_y);

            PixelIndexLUT[x][y] = index;
            PixelBitLUT  [x][y] = (1 << bit);
        }
    }
}

void DisplayPort::InitLandscapeLUT() {
    uint16_t H4 = height_ >> 2;

    for (uint16_t y = 0; y < height_; y++)
    {
        uint16_t inv_y = height_ - 1 - y;
        uint16_t block_y = inv_y >> 2;
        uint8_t  local_y  = inv_y & 3;

        for (uint16_t x = 0; x < width_; x++)
        {
            uint16_t byte_x = x >> 1;
            uint8_t  local_x = x & 1;

            uint32_t index = byte_x * H4 + block_y;
            uint8_t bit = 7 - ((local_y << 1) | local_x);

            PixelIndexLUT[x][y] = index;
            PixelBitLUT  [x][y] = (1 << bit);
        }
    }
}

void DisplayPort::RLCD_SetPixel(uint16_t x, uint16_t y, uint8_t color) {
    uint32_t idx = PixelIndexLUT[x][y];
    uint8_t  mask = PixelBitLUT[x][y];

    uint8_t *p = &DispBuffer[idx];

    if (color)
        *p |= mask;
    else
        *p &= ~mask;
}

#endif
