#include "clock_app.h"
#include "bpc_protocol.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/time.h>

// BSP uses timer/channel 1 for power ramp and 0 for the optional motor.
#define TX_TIMER LEDC_TIMER_2
#define TX_CHANNEL LEDC_CHANNEL_2
#define TX_MODE LEDC_LOW_SPEED_MODE
static esp_timer_handle_t edge_timer;
static TaskHandle_t tx_task;
static bpc_edge_t edges[39];
static unsigned edge_index;
static int64_t frame_monotonic;
static int64_t frame_utc;
static unsigned frame_generation;
static int64_t wall_us(void)
{
    bool usable;
    return clock_time_us(&usable);
}
static bool time_usable(void)
{
    bool usable;
    clock_time_us(&usable);
    return usable;
}
static esp_err_t carrier(bool on)
{
    // The timer owns this channel until it hands control back to the worker.
    // 1-bit PWM gives 50% at duty=1 and the finest fractional frequency setting.
    esp_err_t err = ledc_set_duty(TX_MODE, TX_CHANNEL, on ? 1 : 0);
    return err == ESP_OK ? ledc_update_duty(TX_MODE, TX_CHANNEL) : err;
}
static void stop_frame(esp_err_t err)
{
    carrier(false);
    atomic_store(&tx_active, false);
    if (err != ESP_OK) {
        atomic_store(&tx_error, err);
        atomic_store(&tx_requested, false);
    }
    xTaskNotifyGive(tx_task);
}
static void envelope_edge(void *arg)
{
    (void)arg;
    if (!atomic_load(&tx_requested) || !time_usable() ||
        frame_generation != atomic_load(&clock_generation)) { stop_frame(ESP_OK); return; }
    // Index 39 is the end-of-frame handoff. Keep carrier on across P0.
    if (edge_index == 39) { xTaskNotifyGive(tx_task); return; }
    int64_t late = esp_timer_get_time() - (frame_monotonic + edges[edge_index].at_us);
    if (late > 10000) {
        ESP_LOGW("bpc", "late envelope by %lld us; resynchronizing", (long long)late);
        stop_frame(ESP_OK);
        return;
    }
    esp_err_t err = carrier(edges[edge_index].carrier);
    if (err != ESP_OK) { stop_frame(err); return; }
    if (edge_index == 0) {
        atomic_store(&tx_frame_utc, frame_utc);
        atomic_store(&tx_active, true);
    }
    ++edge_index;
    const int64_t deadline = frame_monotonic + (edge_index < 39 ? edges[edge_index].at_us : 20000000);
    int64_t delay = deadline - esp_timer_get_time();
    err = esp_timer_start_once(edge_timer, delay > 0 ? delay : 1);
    if (err != ESP_OK) stop_frame(err);
}
static esp_err_t configure_output(bool replace)
{
    if (replace) {
        carrier(false);
        const ledc_channel_config_t remove = {.speed_mode = TX_MODE, .channel = TX_CHANNEL, .deconfigure = true};
        esp_err_t err = ledc_channel_config(&remove);
        if (err != ESP_OK) return err;
        gpio_reset_pin(atomic_load(&active_gpio));
    }
    const int pin = atomic_load(&output_gpio), drive = atomic_load(&output_drive);
    const ledc_channel_config_t config = {.gpio_num = pin, .speed_mode = TX_MODE,
        .channel = TX_CHANNEL, .timer_sel = TX_TIMER, .duty = 0};
    esp_err_t err = ledc_channel_config(&config);
    if (err == ESP_OK) err = gpio_od_disable(pin);
    if (err == ESP_OK) err = gpio_set_drive_capability(pin, (gpio_drive_cap_t)drive);
    if (err == ESP_OK) {
        atomic_store(&active_gpio, pin);
        atomic_store(&active_drive, drive);
    }
    return err;
}
static void transmitter(void *arg)
{
    (void)arg;
    int64_t next = 0;
    while (true) {
        frame_generation = atomic_load(&clock_generation);
        if (atomic_load(&output_gpio) != atomic_load(&active_gpio) ||
            atomic_load(&output_drive) != atomic_load(&active_drive)) {
            atomic_store(&tx_active, false);
            esp_err_t err = configure_output(true);
            if (err != ESP_OK) {
                atomic_store(&tx_error, err);
                atomic_store(&tx_requested, false);
                vTaskDelete(NULL);
            }
            next = 0;
        }
        if (!atomic_load(&tx_requested) || !time_usable()) {
            // No timer is pending while the worker owns the channel. Also
            // covers STOP pressed during the end-of-frame handoff.
            carrier(false);
            atomic_store(&tx_active, false);
            next = 0;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        const int64_t now = wall_us();
        if (!next || now - next > 10000 || next - now > 20000000 || !atomic_load(&tx_active)) {
            next = (now / 20000000 + 1) * 20000000;
        }
        frame_utc = next / 1000000;
        const time_t transmitted = frame_utc + BPC_TIMEZONE_SECONDS;
        struct tm bt;
        gmtime_r(&transmitted, &bt);
        uint16_t off_ms[20];
        if (!bpc_encode(&bt, off_ms) || !bpc_make_edges(off_ms, edges)) {
            atomic_store(&tx_error, ESP_ERR_INVALID_ARG);
            atomic_store(&tx_requested, false);
            continue;
        }
        // Anchor all 39 edges to one monotonic epoch. No accumulating sleep drift.
        // RTC changes during a frame are incorporated into the following frame.
        int64_t delay = next - wall_us();
        frame_monotonic = esp_timer_get_time() + delay;
        edge_index = 0;
        esp_err_t err = esp_timer_start_once(edge_timer, delay > 0 ? delay : 1);
        if (err != ESP_OK) {
            atomic_store(&tx_error, err);
            atomic_store(&tx_requested, false);
            continue;
        }
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        next += 20000000;
    }
}
esp_err_t bpc_tx_start(void)
{
    if (!clock_output_pin_valid(atomic_load(&output_gpio))) return ESP_ERR_INVALID_ARG;
    // XTAL matches the BSP power-ramp clock selection on S31, avoiding a
    // shared LEDC clock-source conflict. Fractional divisor error < 1 Hz.
    const ledc_timer_config_t timer = {.speed_mode = TX_MODE, .timer_num = TX_TIMER,
        .duty_resolution = LEDC_TIMER_1_BIT, .freq_hz = BPC_CARRIER_HZ,
        .clk_cfg = LEDC_USE_XTAL_CLK};
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) return err;
    if ((err = configure_output(false)) != ESP_OK) return err;
    const esp_timer_create_args_t args = {.callback = envelope_edge, .name = "bpc_envelope"};
    if ((err = esp_timer_create(&args, &edge_timer)) != ESP_OK) return err;
    ESP_LOGI("bpc", "GPIO%d push-pull, LEDC reports %lu Hz",
        atomic_load(&active_gpio), (unsigned long)ledc_get_freq(TX_MODE, TX_TIMER));
    return xTaskCreate(transmitter, "bpc_tx", 4096, NULL, 12, &tx_task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
