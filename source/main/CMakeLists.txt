idf_component_register(
    SRCS
    "main.cpp"
    "ui/ui_desktop.cpp"
    "ui/font_chinese_16.c"
    "ui/font_num_48.c"
    "ui/font_num_64.c"
    "ui/font_num_28.c"
    "ui/wx_icons.c"
    "data/data_store.cpp"
    "data/data_tasks.cpp"
    "quota/ark_quota.cpp"
    "key/key_bsp.cpp"
    INCLUDE_DIRS
    "."
    "./ui"
    "./data"
    "./quota"
    "./key"
    PRIV_REQUIRES
    rlcd_bsp
    rlcd_app
    nvs_flash
    esp_wifi
    esp_event
    esp_netif
    esp_http_client
    esp-tls
    mbedtls
    json
    esp_timer
    lwip
    REQUIRES
    lvgl__lvgl
)

# font_chinese_16.c 由 lv_font_conv 生成：该宏让它直接 #include "lvgl.h"
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
