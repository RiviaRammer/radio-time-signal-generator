#include "clock_app.h"
#include "bsp/esp_mosaico.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

atomic_bool clock_synced, clock_wifi_up, tx_active;
atomic_bool tx_requested = true;
atomic_int tx_error, wifi_reason;
atomic_int_fast64_t last_sync_us, tx_frame_utc;

void app_main(void)
{
    // No esp_iris, TinyUSB, microphone or BOOT-button driver is started.
    bool led = bsp_led_init() == ESP_OK;
    if (led) bsp_led_set(true);
    esp_err_t err = clock_ui_start();
    if (err != ESP_OK) {
        ESP_LOGE("bpc", "display startup failed: %s", esp_err_to_name(err));
        while (true) {
            if (led) bsp_led_set(true);
            vTaskDelay(pdMS_TO_TICKS(100));
            if (led) bsp_led_set(false);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    if (led) bsp_led_set(false);
    err = clock_settings_init();
    if (err != ESP_OK) ESP_LOGE("clock", "settings: %s", esp_err_to_name(err));
    err = bpc_tx_start();
    atomic_store(&tx_error, err);
    err = clock_network_start();
    if (err != ESP_OK) {
        atomic_store(&wifi_reason, -err);
        ESP_LOGE("bpc", "network startup failed: %s", esp_err_to_name(err));
    }
    while (true) {
        clock_ui_refresh();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
