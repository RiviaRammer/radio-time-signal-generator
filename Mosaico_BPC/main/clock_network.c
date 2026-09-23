#include "clock_app.h"
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

typedef struct { bool scan; char ssid[33], password[65]; } network_command_t;
static QueueHandle_t commands;
static SemaphoreHandle_t state_lock;
static clock_network_state_t state;
static atomic_uint events;
static atomic_int disconnect_reason, scan_status;
enum { EVENT_DOWN = 1, EVENT_IP = 2, EVENT_SCAN = 4 };
static wifi_config_t target;
static bool target_saved;

static void time_received(struct timeval *tv)
{
    bool valid = atomic_load(&clock_wifi_up) && tv->tv_sec >= 1704067200LL && tv->tv_sec < 4102444800LL;
    if (valid) atomic_store(&last_sync_us, esp_timer_get_time());
    atomic_store(&clock_synced, valid);
}
static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&disconnect_reason, ((wifi_event_sta_disconnected_t *)data)->reason);
        atomic_store(&clock_wifi_up, false);
        atomic_store(&clock_synced, false);
        atomic_fetch_or(&events, EVENT_DOWN);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        atomic_store(&scan_status, ((wifi_event_sta_scan_done_t *)data)->status);
        atomic_fetch_or(&events, EVENT_SCAN);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) atomic_fetch_or(&events, EVENT_IP);
}
void clock_network_state(clock_network_state_t *out)
{
    if (!state_lock) { memset(out, 0, sizeof(*out)); snprintf(out->message, sizeof(out->message), "WLAN unavailable (%d)", atomic_load(&wifi_reason)); return; }
    xSemaphoreTake(state_lock, portMAX_DELAY);
    *out = state;
    xSemaphoreGive(state_lock);
}
static void publish(const clock_network_state_t *current)
{
    xSemaphoreTake(state_lock, portMAX_DELAY); state = *current; xSemaphoreGive(state_lock);
}
esp_err_t clock_network_scan(void)
{
    if (!commands) return ESP_ERR_INVALID_STATE;
    network_command_t c = {.scan = true};
    return xQueueSend(commands, &c, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
esp_err_t clock_network_connect(const char *ssid, const char *password)
{
    if (!commands) return ESP_ERR_INVALID_STATE;
    if (!ssid || !password || !ssid[0] || strlen(ssid) > 32 || strlen(password) > 64) return ESP_ERR_INVALID_ARG;
    size_t len = strlen(password);
    if (len && len < 8) return ESP_ERR_INVALID_ARG;
    if (len == 64 && strspn(password, "0123456789abcdefABCDEF") != 64) return ESP_ERR_INVALID_ARG;
    network_command_t c = {0};
    memcpy(c.ssid, ssid, strlen(ssid)); memcpy(c.password, password, len);
    return xQueueSend(commands, &c, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
static esp_err_t save_credentials(const wifi_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("bpc_wifi", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    network_command_t credentials = {0};
    memcpy(credentials.ssid, cfg->sta.ssid, 32); memcpy(credentials.password, cfg->sta.password, 64);
    err = nvs_set_blob(h, "credentials", &credentials, sizeof(credentials));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
static bool load_credentials(wifi_config_t *cfg)
{
    nvs_handle_t h;
    if (nvs_open("bpc_wifi", NVS_READONLY, &h) != ESP_OK) return false;
    network_command_t saved = {0}; size_t size = sizeof(saved);
    esp_err_t err = nvs_get_blob(h, "credentials", &saved, &size); nvs_close(h);
    if (err != ESP_OK || size != sizeof(saved) || !saved.ssid[0]) return false;
    memcpy(cfg->sta.ssid, saved.ssid, 32); memcpy(cfg->sta.password, saved.password, 64);
    return true;
}
static void network_worker(void *arg)
{
    (void)arg;
    clock_network_state_t current = {.ready = true, .saved = target_saved};
    memcpy(current.ssid, target.sta.ssid, 32);
    snprintf(current.message, sizeof(current.message), "Ready to scan");
    bool have_target = target.sta.ssid[0], pending_save = false;
    int64_t retry_at = 0, connect_deadline = 0, scan_deadline = 0;
    for (;;) {
        const int64_t now = esp_timer_get_time();
        unsigned flags = atomic_exchange(&events, 0);
        if (flags & EVENT_DOWN) {
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
                current.connected = current.connecting = false;
                atomic_store(&wifi_reason, atomic_load(&disconnect_reason));
                if (!current.scanning) snprintf(current.message, sizeof(current.message),
                    "Connection failed (%d); retrying", atomic_load(&disconnect_reason));
                retry_at = now + 5000000;
            }
        }
        if (flags & EVENT_IP) {
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK && memcmp(ap.ssid, target.sta.ssid, 32) == 0) {
                current.connected = true; current.connecting = false;
                atomic_store(&clock_wifi_up, true); atomic_store(&wifi_reason, 0);
                esp_netif_sntp_start(); // Re-query on every new IP after reconnect.
                esp_err_t err = pending_save ? save_credentials(&target) : ESP_OK;
                if (pending_save && err == ESP_OK) { current.saved = true; pending_save = false; }
                if (err == ESP_OK) snprintf(current.message, sizeof(current.message), "%s", current.saved ? "Connected / saved" : "Connected");
                else snprintf(current.message, sizeof(current.message), "Connected; save failed (0x%x)", err);
            }
        }
        if ((flags & EVENT_SCAN) && current.scanning) {
            wifi_ap_record_t found[CLOCK_AP_MAX]; uint16_t count = CLOCK_AP_MAX;
            esp_err_t err = esp_wifi_scan_get_ap_records(&count, found);
            current.scanning = false; current.count = 0;
            if (err == ESP_OK && atomic_load(&scan_status) == 0) {
                for (unsigned i = 0; i < count; ++i) {
                    if (!found[i].ssid[0]) continue;
                    bool duplicate = false;
                    for (unsigned j = 0; j < current.count; ++j)
                        if (memcmp(current.aps[j].ssid, found[i].ssid, 32) == 0) duplicate = true;
                    if (duplicate) continue;
                    clock_ap_t *ap = &current.aps[current.count++];
                    memset(ap, 0, sizeof(*ap)); memcpy(ap->ssid, found[i].ssid, 32);
                    ap->rssi = found[i].rssi; ap->open = found[i].authmode == WIFI_AUTH_OPEN;
                    ap->supported = ap->open || found[i].authmode == WIFI_AUTH_WPA_PSK ||
                        found[i].authmode == WIFI_AUTH_WPA2_PSK || found[i].authmode == WIFI_AUTH_WPA_WPA2_PSK ||
                        found[i].authmode == WIFI_AUTH_WPA3_PSK || found[i].authmode == WIFI_AUTH_WPA2_WPA3_PSK;
                }
                snprintf(current.message, sizeof(current.message), "%u networks; select to connect", current.count);
            } else snprintf(current.message, sizeof(current.message), "Scan failed; tap Scan to retry");
            ++current.revision; retry_at = now + 1500000;
        }
        if (current.scanning && now > scan_deadline) {
            esp_wifi_scan_stop(); esp_wifi_clear_ap_list(); current.scanning = false;
            snprintf(current.message, sizeof(current.message), "Scan timed out; retry"); ++current.revision;
        }
        network_command_t command;
        if (xQueueReceive(commands, &command, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (command.scan && !current.scanning) {
                if (!current.connected) esp_wifi_disconnect();
                wifi_scan_config_t scan = {.show_hidden = false};
                esp_err_t err = esp_wifi_scan_start(&scan, false);
                current.scanning = err == ESP_OK; current.connecting = false;
                scan_deadline = now + 20000000;
                if (err == ESP_OK) snprintf(current.message, sizeof(current.message), "Scanning...");
                else snprintf(current.message, sizeof(current.message), "Scan failed (0x%x)", err);
            } else if (!command.scan) {
                if (current.scanning) { esp_wifi_scan_stop(); esp_wifi_clear_ap_list(); current.scanning = false; }
                esp_wifi_disconnect(); memset(&target, 0, sizeof(target));
                memcpy(target.sta.ssid, command.ssid, 32); memcpy(target.sta.password, command.password, 64);
                target.sta.pmf_cfg.capable = true;
                esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &target);
                have_target = err == ESP_OK; pending_save = have_target;
                current.saved = current.connected = current.connecting = false;
                atomic_store(&clock_wifi_up, false);
                atomic_store(&clock_synced, false);
                memcpy(current.ssid, command.ssid, sizeof(current.ssid)); retry_at = now + 300000;
                if (err == ESP_OK) snprintf(current.message, sizeof(current.message), "Connecting...");
                else snprintf(current.message, sizeof(current.message), "WLAN config failed (0x%x)", err);
                memset(&command, 0, sizeof(command));
            }
        }
        if (current.connecting && now > connect_deadline) {
            esp_wifi_disconnect(); current.connecting = false; retry_at = now + 5000000;
            snprintf(current.message, sizeof(current.message), "Connection timed out; retrying");
        }
        if (have_target && !current.scanning && !current.connected && !current.connecting && now >= retry_at) {
            esp_err_t err = esp_wifi_connect(); current.connecting = err == ESP_OK;
            connect_deadline = now + 20000000; retry_at = now + 5000000;
            if (err != ESP_OK) snprintf(current.message, sizeof(current.message), "Connect failed (0x%x); retrying", err);
        }
        publish(&current);
    }
}
esp_err_t clock_network_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err;
    if ((err = esp_netif_init()) != ESP_OK) return err;
    if ((err = esp_event_loop_create_default()) != ESP_OK) return err;
    if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    if ((err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, NULL)) != ESP_OK) return err;
    if ((err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event, NULL)) != ESP_OK) return err;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&init)) != ESP_OK) return err;
    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) return err;
    target_saved = load_credentials(&target);
    if (!target_saved) {
        memcpy(target.sta.ssid, BPC_WIFI_SSID, strnlen(BPC_WIFI_SSID, 32));
        memcpy(target.sta.password, BPC_WIFI_PASSWORD, strnlen(BPC_WIFI_PASSWORD, 64));
    }
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) return err;
    if (target.sta.ssid[0] && (err = esp_wifi_set_config(WIFI_IF_STA, &target)) != ESP_OK) return err;
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3,
        ESP_SNTP_SERVER_LIST(BPC_NTP_SERVER_1, BPC_NTP_SERVER_2, BPC_NTP_SERVER_3));
    sntp.start = false; // Wait until the station has an IP address.
    sntp.sync_cb = time_received;
    if ((err = esp_netif_sntp_init(&sntp)) != ESP_OK) return err;
    if ((err = esp_wifi_start()) != ESP_OK) return err;
    if ((err = esp_wifi_set_ps(WIFI_PS_NONE)) != ESP_OK) return err;
    state_lock = xSemaphoreCreateMutex();
    if (!state_lock) return ESP_ERR_NO_MEM;
    commands = xQueueCreate(4, sizeof(network_command_t));
    if (!commands) return ESP_ERR_NO_MEM;
    if (xTaskCreate(network_worker, "wlan", 12288, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(commands); commands = NULL; return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

