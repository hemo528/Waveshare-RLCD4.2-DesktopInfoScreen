// wx_icons.h —— 由 tools/sim/gen_icons.py 自动生成，勿手改
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// 图标枚举（data_store.wx_icon 使用）
enum {
    WX_ICON_SUNNY = 0,
    WX_ICON_PARTLY,
    WX_ICON_OVERCAST,
    WX_ICON_FOG,
    WX_ICON_RAIN_L,
    WX_ICON_RAIN_H,
    WX_ICON_SLEET,
    WX_ICON_SNOW,
    WX_ICON_THUNDER,
    WX_ICON_COUNT,
};

extern const lv_img_dsc_t icon_sunny_a,    icon_sunny_b;
extern const lv_img_dsc_t icon_partly_a,   icon_partly_b;
extern const lv_img_dsc_t icon_overcast_a, icon_overcast_b;
extern const lv_img_dsc_t icon_fog_a,      icon_fog_b;
extern const lv_img_dsc_t icon_rain_l_a,   icon_rain_l_b;
extern const lv_img_dsc_t icon_rain_h_a,   icon_rain_h_b;
extern const lv_img_dsc_t icon_sleet_a,    icon_sleet_b;
extern const lv_img_dsc_t icon_snow_a,     icon_snow_b;
extern const lv_img_dsc_t icon_thunder_a,  icon_thunder_b;

// wx_icon 枚举值 → 帧 A/B 图源
const lv_img_dsc_t *wx_icon_frame(int icon, int frame);

#ifdef __cplusplus
}
#endif
