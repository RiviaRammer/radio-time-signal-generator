idf_component_register(SRCS "main.c" "bpc_protocol.c" "bpc_tx.c" "clock_network.c" "clock_ui.c" "clock_settings.c"
    INCLUDE_DIRS "."
    REQUIRES esp-mosaico-bsp esp_wifi esp_netif esp_event nvs_flash esp_timer
             esp_driver_ledc esp_driver_gpio lwip)
