#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef int nvs_handle_t;
#define NVS_READWRITE 1
#define NVS_READONLY 0
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *h);
esp_err_t nvs_set_i32(nvs_handle_t h, const char *key, int32_t value);
esp_err_t nvs_get_i32(nvs_handle_t h, const char *key, int32_t *value);
esp_err_t nvs_commit(nvs_handle_t h);
void nvs_close(nvs_handle_t h);
