# 中间层：LVGL 移植 + WiFi STA
idf_component_register(
    SRCS
    "lvgl_bsp.cpp"
    "wifi_sta_bsp.cpp"
    INCLUDE_DIRS
    "./"
    PRIV_REQUIRES
    nvs_flash
    esp_wifi
    esp_event
    esp_netif
    esp_timer
    rlcd_bsp
    lwip
    REQUIRES
    lvgl__lvgl
)
