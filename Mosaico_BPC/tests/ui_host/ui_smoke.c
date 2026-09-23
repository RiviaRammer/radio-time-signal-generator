#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sys/time.h"
#include "nvs.h"
static struct tm *sim_gmtime_r(const time_t *s, struct tm *t) { return gmtime_s(t, s) == 0 ? t : NULL; }
#define gmtime_r sim_gmtime_r
#include "../../main/clock_ui.c"

atomic_bool clock_synced, clock_wifi_up, tx_requested = true, tx_active;
atomic_int tx_error, wifi_reason;
atomic_int_fast64_t last_sync_us, tx_frame_utc;
static int64_t mono = 1000000, utc = 1790164800000000LL;
static int32_t saved_mode = -1, saved_pin = -1, saved_drive = -1;
static bool fail_commit;
static clock_network_state_t mock_net;
static int scans, connects;
static char connected_ssid[33], connected_password[65];
static uint8_t buffer[480 * 480 * 4], pixels[480 * 480 * 3];
int64_t esp_timer_get_time(void) { return mono; }
int gettimeofday(struct timeval *tv, void *tz) { (void)tz; tv->tv_sec = utc / 1000000; tv->tv_usec = (long)(utc % 1000000); return 0; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_open(const char *n, int m, nvs_handle_t *h) { (void)n; (void)m; *h = 1; return ESP_OK; }
esp_err_t nvs_set_i32(nvs_handle_t h, const char *key, int32_t value)
{
    (void)h;
    if (fail_commit) return ESP_FAIL;
    if (!strcmp(key, "mode")) saved_mode = value;
    if (!strcmp(key, "pin")) saved_pin = value;
    if (!strcmp(key, "drive")) saved_drive = value;
    return ESP_OK;
}
esp_err_t nvs_get_i32(nvs_handle_t h, const char *key, int32_t *value)
{
    (void)h;
    *value = !strcmp(key, "mode") ? saved_mode : !strcmp(key, "pin") ? saved_pin : saved_drive;
    return *value < 0 ? ESP_FAIL : ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return fail_commit ? ESP_FAIL : ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
void clock_network_state(clock_network_state_t *out) { *out = mock_net; }
esp_err_t clock_network_scan(void) { ++scans; mock_net.scanning = true; return ESP_OK; }
esp_err_t clock_network_connect(const char *ssid, const char *pw)
{
    if (*pw && strlen(pw) < 8) return ESP_ERR_INVALID_ARG;
    ++connects; strcpy(connected_ssid, ssid); strcpy(connected_password, pw);
    mock_net.connected = true; mock_net.saved = true; strcpy(mock_net.ssid, ssid);
    strcpy(mock_net.message, "Connected / saved"); return ESP_OK;
}
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *map)
{
    for (int y = area->y1; y <= area->y2; ++y) for (int x = area->x1; x <= area->x2; ++x) {
        size_t source = ((y-area->y1) * (area->x2-area->x1+1) + x-area->x1) * 4;
        size_t dest = (y*480+x)*3;
        pixels[dest] = map[source+2]; pixels[dest+1] = map[source+1]; pixels[dest+2] = map[source];
    }
    lv_display_flush_ready(display);
}
lv_display_t *bsp_display_start_with_config(const bsp_display_config_t *cfg)
{
    (void)cfg; lv_init();
    lv_display_t *display = lv_display_create(480,480);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush); return display;
}
esp_err_t bsp_display_brightness_set(int value) { (void)value; return ESP_OK; }
bool bsp_display_lock(int timeout) { (void)timeout; return true; }
void bsp_display_unlock(void) {}
static lv_obj_t *find_button(lv_obj_t *p, const char *text)
{
    if (lv_obj_check_type(p, &lv_button_class)) {
        for (unsigned i = 0; i < lv_obj_get_child_count(p); ++i) {
            lv_obj_t *c = lv_obj_get_child(p, i);
            if (lv_obj_check_type(c, &lv_label_class) && !strcmp(lv_label_get_text(c), text)) return p;
        }
    }
    for (unsigned i = 0; i < lv_obj_get_child_count(p); ++i) {
        lv_obj_t *match = find_button(lv_obj_get_child(p,i), text); if (match) return match;
    }
    return NULL;
}
static void click(const char *text)
{
    lv_obj_t *b = find_button(root, text); assert(b && !lv_obj_has_state(b, LV_STATE_DISABLED));
    lv_obj_send_event(b, LV_EVENT_CLICKED, NULL);
    clock_ui_refresh();
}
static void capture(const char *name)
{
    clock_ui_refresh(); lv_obj_update_layout(root); lv_tick_inc(40); lv_timer_handler(); lv_refr_now(NULL);
    if (keyboard) {
        lv_area_t bounds; lv_obj_get_coords(keyboard, &bounds);
        assert(bounds.y1 >= 260 && bounds.y2 < 480 && bounds.x1 == 0 && bounds.x2 == 479);
    }
    char file[100]; snprintf(file, sizeof(file), "%s.ppm", name);
    FILE *f = fopen(file,"wb"); assert(f); fprintf(f, "P6\n480 480\n255\n"); fwrite(pixels,1,sizeof(pixels),f); fclose(f);
}

int main(void)
{
    saved_mode = 1; // A legacy manual-mode NVS value must have no effect.
    assert(clock_settings_init() == ESP_OK);
    assert(atomic_load(&output_gpio) == 38);
    bool valid;
    atomic_store(&clock_synced,true); atomic_store(&last_sync_us,mono);
    clock_time_us(&valid); assert(!valid); // NTP from a previous connection is insufficient.
    atomic_store(&clock_wifi_up,true);
    assert(clock_time_us(&valid) == utc && valid);
    mono += (int64_t)BPC_NTP_MAX_AGE_SECONDS * 1000000;
    clock_time_us(&valid); assert(!valid);
    atomic_store(&last_sync_us,mono);
    const int allowed[] = {4,5,10,11,12,13,14,15,16,17,18,19,37,38,39,40,46,47,48,49,52,53,54,55};
    for (unsigned i=0;i<sizeof(allowed)/sizeof(allowed[0]);++i) assert(clock_output_pin_valid(allowed[i]));
    const int forbidden[] = {-1,0,1,2,3,6,7,8,9,20,21,22,23,24,25,35,36,42,43,44,45,50,51,56,57,58,60,61,64};
    for (unsigned i=0;i<sizeof(forbidden)/sizeof(forbidden[0]);++i)
        assert(clock_set_output(forbidden[i],2) == ESP_ERR_INVALID_ARG);
    fail_commit = true;
    assert(clock_set_output(15,3) != ESP_OK && atomic_load(&output_gpio) == 38);
    fail_commit = false;
    assert(clock_set_output(38,4) == ESP_ERR_INVALID_ARG);
    mock_net.ready = true; strcpy(mock_net.message,"Connected / saved");
    mock_net.count = 3; mock_net.revision = 1;
    mock_net.aps[0] = (clock_ap_t){.ssid="Home WLAN",.rssi=-43,.supported=true};
    mock_net.aps[1] = (clock_ap_t){.ssid="Guest network",.rssi=-67,.open=true,.supported=true};
    mock_net.aps[2] = (clock_ap_t){.ssid="Enterprise",.rssi=-74,.supported=false};
    assert(clock_ui_start() == ESP_OK);
    atomic_store(&clock_wifi_up,false); capture("01-offline");
    assert(!strcmp(lv_label_get_text(time_label),"No network"));
    assert(lv_color_to_u32(lv_obj_get_style_text_color(time_label,0)) == lv_color_to_u32(lv_color_hex(RED)));
    assert(lv_obj_get_style_text_font(time_label,0) == &lv_font_montserrat_40);
    atomic_store(&clock_wifi_up,true); atomic_store(&clock_synced,false); capture("01-syncing");
    assert(!strcmp(lv_label_get_text(time_label),"Syncing..."));
    atomic_store(&clock_synced,true); capture("01-home");
    assert(strstr(lv_label_get_text(date_label),"UTC") == NULL);
    click("STOP TX"); assert(!atomic_load(&tx_requested)); click("START TX"); assert(atomic_load(&tx_requested));
    click("Setting"); capture("02-settings");
    assert(find_button(root,"Time mode") == NULL);
    click("Protocol"); capture("03-protocol");
    assert(lv_obj_has_state(find_button(root,"WWVB / USA / 60 kHz"),LV_STATE_DISABLED));
    assert(lv_obj_has_state(find_button(root,"DCF77 / Europe / 77.5 kHz"),LV_STATE_DISABLED));
    assert(lv_obj_has_state(find_button(root,"MSF / UK / 60 kHz"),LV_STATE_DISABLED));
    click("Back"); click("Output"); click("GPIO38  >");
    lv_textarea_set_text(input,""); click("Apply GPIO"); assert(page == PAGE_GPIO);
    lv_textarea_set_text(input,"42"); click("Apply GPIO"); assert(page == PAGE_GPIO && atomic_load(&output_gpio)==38);
    lv_textarea_set_text(input,""); lv_buttonmatrix_set_selected_button(keyboard,0);
    lv_obj_send_event(keyboard,LV_EVENT_VALUE_CHANGED,NULL); assert(lv_textarea_get_text(input)[0]);
    lv_textarea_set_text(input,"53"); capture("07-gpio");
    lv_obj_send_event(keyboard,LV_EVENT_READY,NULL); assert(page == PAGE_OUTPUT && atomic_load(&output_gpio)==53);
    lv_dropdown_set_selected(drive_select,3); click("Save output");
    assert(saved_pin==53 && saved_drive==3); capture("07-output");
    click("Back"); click("WLAN"); clock_ui_refresh(); capture("08-wlan");
    assert(lv_obj_has_state(find_button(root,"Enterprise"),LV_STATE_DISABLED));
    click("Scan"); clock_ui_refresh(); assert(scans == 1 && lv_obj_has_state(scan_button,LV_STATE_DISABLED));
    mock_net.scanning = false; ++mock_net.revision; clock_ui_refresh();
    click("Home WLAN"); lv_textarea_set_text(input,"wrong"); click("Connect & save"); assert(page == PAGE_PASSWORD && connects == 0);
    lv_textarea_set_text(input,"test-password"); capture("09-password");
    lv_obj_send_event(keyboard,LV_EVENT_READY,NULL); assert(page == PAGE_WLAN && connects == 1);
    assert(!strcmp(connected_ssid,"Home WLAN") && !strcmp(connected_password,"test-password"));
    click("Guest network"); assert(input == NULL); click("Connect & save"); assert(connects == 2 && !connected_password[0]);
    capture("10-connected");
    for(int i=0;i<40;++i) {
        show_page(PAGE_GPIO); lv_obj_send_event(keyboard,LV_EVENT_CANCEL,NULL); assert(page==PAGE_OUTPUT);
        show_page(PAGE_PASSWORD); assert(input==NULL); show_page(PAGE_SETTINGS);
    }
    puts("PASS: NTP-only/offline/freshness states, red No network status, 24 GPIOs and reserved-pin rejection, keyboard input, disabled protocols, WLAN flows and saved output");
    return 0;
}
