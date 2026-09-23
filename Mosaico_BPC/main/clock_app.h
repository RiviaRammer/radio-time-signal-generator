#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include "esp_err.h"
#if __has_include("config.h")
#include "config.h"
#else
#include "config_example.h"
#endif

extern atomic_bool clock_synced, clock_wifi_up, tx_requested, tx_active;
extern atomic_int tx_error, wifi_reason;
extern atomic_int_fast64_t last_sync_us, tx_frame_utc;
extern atomic_uint clock_generation;
extern atomic_int output_gpio, output_drive, active_gpio, active_drive;
esp_err_t clock_settings_init(void);
int64_t clock_time_us(bool *usable);
esp_err_t clock_set_output(int gpio, int drive);
bool clock_output_pin_valid(int gpio);

#define CLOCK_AP_MAX 20
typedef struct { char ssid[33]; int rssi; bool open, supported; } clock_ap_t;
typedef struct {
    bool ready, scanning, connecting, connected, saved;
    unsigned revision;
    unsigned count;
    clock_ap_t aps[CLOCK_AP_MAX];
    char ssid[33], message[100];
} clock_network_state_t;
void clock_network_state(clock_network_state_t *out);
esp_err_t clock_network_scan(void);
esp_err_t clock_network_connect(const char *ssid, const char *password);
esp_err_t clock_ui_start(void);
void clock_ui_refresh(void);
esp_err_t clock_network_start(void);
esp_err_t bpc_tx_start(void);
