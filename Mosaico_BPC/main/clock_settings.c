#include "clock_app.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <sys/time.h>

atomic_uint clock_generation;
atomic_int output_gpio = BPC_TX_GPIO, output_drive = BPC_TX_DRIVE;
atomic_int active_gpio = BPC_TX_GPIO, active_drive = BPC_TX_DRIVE;
static bool nvs_ready;

bool clock_output_pin_valid(int pin)
{
    // BSP subboard.h/subboard.c: H connectors plus extended left/right pairs.
    // Audio and expansion-module drivers are not started by this application.
    static const int pins[] = {4,5,10,11,12,13,14,15,16,17,18,19,37,38,39,40,46,47,48,49,52,53,54,55};
    for (unsigned i = 0; i < sizeof(pins)/sizeof(pins[0]); ++i) if (pin == pins[i]) return true;
    return false;
}
esp_err_t clock_settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err;
    nvs_ready = true;
    nvs_handle_t h;
    if (nvs_open("bpc_settings", NVS_READONLY, &h) == ESP_OK) {
        int32_t value;
        if (nvs_get_i32(h, "pin", &value) == ESP_OK && clock_output_pin_valid(value)) atomic_store(&output_gpio, value);
        if (nvs_get_i32(h, "drive", &value) == ESP_OK && value >= 0 && value <= 3) atomic_store(&output_drive, value);
        nvs_close(h);
    }
    // Ignore any legacy manual-mode key. Only NTP is supported.
    return ESP_OK;
}
int64_t clock_time_us(bool *usable)
{
    *usable = atomic_load(&clock_wifi_up) && atomic_load(&clock_synced) &&
        esp_timer_get_time() - atomic_load(&last_sync_us) < (int64_t)BPC_NTP_MAX_AGE_SECONDS * 1000000;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}
esp_err_t clock_set_output(int pin, int drive)
{
    if (!clock_output_pin_valid(pin) || drive < 0 || drive > 3) return ESP_ERR_INVALID_ARG;
    if (!nvs_ready) return ESP_ERR_INVALID_STATE;
    nvs_handle_t h;
    esp_err_t err = nvs_open("bpc_settings", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_i32(h, "pin", pin);
    if (err == ESP_OK) err = nvs_set_i32(h, "drive", drive);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) return err;
    atomic_store(&output_gpio, pin);
    atomic_store(&output_drive, drive);
    atomic_fetch_add(&clock_generation, 1);
    return ESP_OK;
}

