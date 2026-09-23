#pragma once
#include "esp_err.h"
#include "lvgl.h"
typedef struct { bool enable_touch; } bsp_display_config_t;
#define BSP_DISPLAY_DEFAULT_CONFIG() ((bsp_display_config_t){0})
lv_display_t *bsp_display_start_with_config(const bsp_display_config_t *cfg);
esp_err_t bsp_display_brightness_set(int value);
bool bsp_display_lock(int timeout);
void bsp_display_unlock(void);
